#include "library/folders/libraryfolderfeature.h"

#include <QDir>
#include <QMap>
#include <QMenu>
#include <QSqlQuery>
#include <QtDebug>
#include <map>
#include <memory>

#include "analyzer/analyzerscheduledtrack.h"
#include "library/dao/playlistdao.h"
#include "library/library.h"
#include "library/preparationplaylist.h"
#include "library/queryutil.h"
#include "library/sidebarmodel.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/trackset/crate/crate.h"
#include "library/treeitem.h"
#include "moc_libraryfolderfeature.cpp"
#include "util/desktophelper.h"
#include "widget/wlibrarysidebar.h"

namespace {

// Marks the sidebar item for the tracks located directly in a library
// root directory (not in a subfolder). File paths never contain it.
const QChar kFolderOnlyMarker = QChar(0x0001);

QString normalizedFolderPath(QString path) {
    while (path.size() > 1 && path.endsWith(QChar('/'))) {
        path.chop(1);
    }
    return path;
}

bool isSameOrSubFolder(const QString& folder, const QString& parent) {
    if (folder == parent) {
        return true;
    }
    if (parent.endsWith(QChar('/'))) { // e.g. "/"
        return folder.startsWith(parent);
    }
    return folder.startsWith(parent) && folder.at(parent.size()) == QChar('/');
}

QString folderName(const QString& path) {
    const QString dirName = QDir(path).dirName();
    return dirName.isEmpty() ? QDir::toNativeSeparators(path) : dirName;
}

QString sortKey(const QString& name, const QString& path) {
    return name.toLower() + QChar('\0') + path;
}

/// Intermediate tree used to aggregate the track counts per folder
/// before the TreeItems are created.
struct FolderNode {
    QString path;
    QString name;
    int directCount = 0; // tracks located directly in this folder
    int totalCount = 0;  // including all subfolders
    // Sorted case-insensitively by name
    std::map<QString, std::unique_ptr<FolderNode>> children;

    FolderNode* child(const QString& childName, const QString& childPath) {
        auto& pChild = children[sortKey(childName, childPath)];
        if (!pChild) {
            pChild = std::make_unique<FolderNode>();
            pChild->name = childName;
            pChild->path = childPath;
        }
        return pChild.get();
    }
};

QString formatLabel(const QString& name, int count) {
    return QStringLiteral("%1 (%2)").arg(name, QString::number(count));
}

void appendTreeItems(TreeItem* pParentItem, const FolderNode& node, const QString& label) {
    TreeItem* pItem = pParentItem->appendChild(label, node.path);
    for (const auto& [key, pChild] : node.children) {
        Q_UNUSED(key);
        appendTreeItems(pItem, *pChild, formatLabel(pChild->name, pChild->totalCount));
    }
}

} // anonymous namespace

LibraryFolderFeature::LibraryFolderFeature(
        Library* pLibrary,
        UserSettingsPointer pConfig)
        : LibraryFeature(pLibrary, pConfig, QStringLiteral("folders")),
          m_pTrackCollection(pLibrary->trackCollectionManager()->internalCollection()),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_folderTableModel(this, pLibrary->trackCollectionManager()) {
    m_pAnalyzeFolderAction = make_parented<QAction>(
            tr("Analyze Folder (incl. Subfolders)"), this);
    connect(m_pAnalyzeFolderAction.get(),
            &QAction::triggered,
            this,
            &LibraryFolderFeature::slotAnalyzeFolder);

    m_pAddToPreparationAction = make_parented<QAction>(
            tr("Add Folder to Preparation"), this);
    connect(m_pAddToPreparationAction.get(),
            &QAction::triggered,
            this,
            &LibraryFolderFeature::slotAddFolderToPreparation);

    m_pCreateCrateAction = make_parented<QAction>(
            tr("Create Crate from Folder"), this);
    connect(m_pCreateCrateAction.get(),
            &QAction::triggered,
            this,
            &LibraryFolderFeature::slotCreateCrateFromFolder);

    m_pAddToAutoDJAction = make_parented<QAction>(
            tr("Add Folder to Auto DJ Queue (bottom)"), this);
    connect(m_pAddToAutoDJAction.get(),
            &QAction::triggered,
            this,
            &LibraryFolderFeature::slotAddFolderToAutoDJ);

    m_pOpenInFileBrowserAction = make_parented<QAction>(
            tr("Open in File Browser"), this);
    connect(m_pOpenInFileBrowserAction.get(),
            &QAction::triggered,
            this,
            &LibraryFolderFeature::slotOpenInFileBrowser);

    m_pRescanLibraryAction = make_parented<QAction>(
            tr("Rescan Library"), this);
    connect(m_pRescanLibraryAction.get(),
            &QAction::triggered,
            this,
            &LibraryFolderFeature::slotRescanLibrary);

    m_pRefreshTreeAction = make_parented<QAction>(
            tr("Refresh Folder List"), this);
    connect(m_pRefreshTreeAction.get(),
            &QAction::triggered,
            this,
            &LibraryFolderFeature::slotRebuildFolderTree);

    slotRebuildFolderTree();
}

QVariant LibraryFolderFeature::title() {
    return tr("Folders");
}

TreeItemModel* LibraryFolderFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void LibraryFolderFeature::bindSidebarWidget(WLibrarySidebar* pSidebarWidget) {
    m_pSidebarWidget = pSidebarWidget;
}

// static
LibraryFolderFeature::FolderRef LibraryFolderFeature::folderRefFromIndex(
        const QModelIndex& index) {
    FolderRef folder;
    if (!index.isValid()) {
        return folder;
    }
    // The index belongs to the SidebarModel, its internal pointer is our
    // TreeItem (same as in CrateFeature).
    const TreeItem* pItem = static_cast<TreeItem*>(index.internalPointer());
    if (!pItem) {
        return folder;
    }
    QString data = pItem->getData().toString();
    if (data.startsWith(kFolderOnlyMarker)) {
        data.remove(0, 1);
        folder.scope = LibraryFolderTableModel::Scope::FolderOnly;
    }
    folder.path = data;
    return folder;
}

QString LibraryFolderFeature::folderDisplayName(const FolderRef& folder) const {
    return folderName(folder.path);
}

void LibraryFolderFeature::slotRebuildFolderTree() {
    // Track counts per directory, only for tracks visible in the library
    QMap<QString, int> directoryCounts;
    {
        QSqlQuery query(m_pTrackCollection->database());
        query.setForwardOnly(true);
        query.prepare(QStringLiteral(
                "SELECT track_locations.directory, COUNT(*) FROM library "
                "INNER JOIN track_locations "
                "ON library.location=track_locations.id "
                "WHERE library.mixxx_deleted=0 "
                "AND track_locations.fs_deleted=0 "
                "GROUP BY track_locations.directory"));
        if (!query.exec()) {
            LOG_FAILED_QUERY(query);
        }
        while (query.next()) {
            const QString directory = normalizedFolderPath(query.value(0).toString());
            if (!directory.isEmpty()) {
                directoryCounts[directory] += query.value(1).toInt();
            }
        }
    }

    QStringList rootDirs;
    const QStringList rootDirStrings = m_pTrackCollection->getRootDirStrings();
    for (const QString& rootDir : rootDirStrings) {
        rootDirs.append(normalizedFolderPath(rootDir));
    }
    // Longest first, so that nested root directories are matched
    // to the most specific one.
    std::sort(rootDirs.begin(), rootDirs.end(), [](const QString& a, const QString& b) {
        return a.size() > b.size();
    });

    // One node per library root directory. Tracks outside of all root
    // directories (e.g. dropped from elsewhere) get a node for their own
    // directory, which is then shown as a regular top level folder.
    std::map<QString, std::unique_ptr<FolderNode>> rootNodes;
    std::map<QString, std::unique_ptr<FolderNode>> outsideNodes;
    auto nodeFor = [](std::map<QString, std::unique_ptr<FolderNode>>* pNodes,
                           const QString& path) {
        auto& pNode = (*pNodes)[sortKey(folderName(path), path)];
        if (!pNode) {
            pNode = std::make_unique<FolderNode>();
            pNode->path = path;
            pNode->name = folderName(path);
        }
        return pNode.get();
    };

    for (auto it = directoryCounts.constBegin(); it != directoryCounts.constEnd(); ++it) {
        const QString& directory = it.key();
        const int count = it.value();

        QString rootDir;
        for (const QString& candidate : std::as_const(rootDirs)) {
            if (isSameOrSubFolder(directory, candidate)) {
                rootDir = candidate;
                break;
            }
        }
        FolderNode* pNode = rootDir.isEmpty()
                ? nodeFor(&outsideNodes, directory)
                : nodeFor(&rootNodes, rootDir);
        if (rootDir.isEmpty()) {
            rootDir = directory;
        }
        pNode->totalCount += count;
        if (directory.size() == rootDir.size()) {
            pNode->directCount += count;
            continue;
        }
        const int offset = rootDir.endsWith(QChar('/'))
                ? rootDir.size()
                : rootDir.size() + 1;
        const QStringList components =
                directory.mid(offset).split(QChar('/'), Qt::SkipEmptyParts);
        QString path = rootDir;
        for (const QString& component : components) {
            if (!path.endsWith(QChar('/'))) {
                path += QChar('/');
            }
            path += component;
            pNode = pNode->child(component, path);
            pNode->totalCount += count;
        }
        pNode->directCount += count;
    }

    // Top level items: the subfolders of all root directories (the root
    // directories themselves are skipped), the directories outside of the
    // root directories and one item per root directory with tracks that
    // are not in any subfolder.
    struct TopLevelEntry {
        const FolderNode* pNode;
        QString rootName; // for disambiguation
    };
    std::map<QString, TopLevelEntry> topLevel;
    QMap<QString, int> nameCounts;
    for (const auto& [key, pRootNode] : rootNodes) {
        Q_UNUSED(key);
        for (const auto& [childKey, pChild] : pRootNode->children) {
            topLevel.emplace(childKey, TopLevelEntry{pChild.get(), pRootNode->name});
            nameCounts[pChild->name.toLower()] += 1;
        }
    }
    for (const auto& [key, pNode] : outsideNodes) {
        topLevel.emplace(key, TopLevelEntry{pNode.get(), QString()});
        nameCounts[pNode->name.toLower()] += 1;
    }

    std::unique_ptr<TreeItem> pRootItem = TreeItem::newRoot(this);
    for (const auto& [key, entry] : topLevel) {
        Q_UNUSED(key);
        QString name = entry.pNode->name;
        if (nameCounts.value(name.toLower()) > 1 && !entry.rootName.isEmpty()) {
            // Same folder name in several root directories
            name = QStringLiteral("%1 – %2").arg(name, entry.rootName);
        }
        appendTreeItems(pRootItem.get(),
                *entry.pNode,
                formatLabel(name, entry.pNode->totalCount));
    }
    for (const auto& [key, pRootNode] : rootNodes) {
        Q_UNUSED(key);
        if (pRootNode->directCount > 0) {
            pRootItem->appendChild(
                    formatLabel(tr("(Tracks directly in %1)").arg(pRootNode->name),
                            pRootNode->directCount),
                    QString(kFolderOnlyMarker + pRootNode->path));
        }
    }
    m_pSidebarModel->setRootItem(std::move(pRootItem));
}

void LibraryFolderFeature::activate() {
    // Show all tracks, like "Tracks", and unfold the folder tree
    emit saveModelState();
    m_folderTableModel.selectFolder(QString(), LibraryFolderTableModel::Scope::AllTracks);
    emit showTrackModel(&m_folderTableModel);
    emit enableCoverArtDisplay(true);
    if (m_pSidebarWidget) {
        auto* pSidebarModel = qobject_cast<SidebarModel*>(m_pSidebarWidget->model());
        if (pSidebarModel) {
            m_pSidebarWidget->expand(pSidebarModel->getFeatureRootIndex(this));
        }
    }
}

void LibraryFolderFeature::activateChild(const QModelIndex& index) {
    const FolderRef folder = folderRefFromIndex(index);
    if (!folder.isValid()) {
        return;
    }
    emit saveModelState();
    m_folderTableModel.selectFolder(folder.path, folder.scope);
    emit showTrackModel(&m_folderTableModel);
    emit enableCoverArtDisplay(true);
    // Like a file manager: selecting a folder also shows its subfolders
    if (m_pSidebarWidget) {
        m_pSidebarWidget->expand(index);
    }
}

void LibraryFolderFeature::onRightClick(const QPoint& globalPos) {
    m_lastRightClickedFolder = FolderRef();
    QMenu menu(m_pSidebarWidget);
    menu.addAction(m_pRefreshTreeAction.get());
    menu.addAction(m_pRescanLibraryAction.get());
    menu.exec(globalPos);
}

void LibraryFolderFeature::onRightClickChild(
        const QPoint& globalPos, const QModelIndex& index) {
    m_lastRightClickedFolder = folderRefFromIndex(index);
    if (!m_lastRightClickedFolder.isValid()) {
        return;
    }
    const bool recursive = m_lastRightClickedFolder.scope ==
            LibraryFolderTableModel::Scope::Recursive;
    m_pAnalyzeFolderAction->setText(recursive
                    ? tr("Analyze Folder (incl. Subfolders)")
                    : tr("Analyze Tracks"));
    m_pAddToPreparationAction->setText(recursive
                    ? tr("Add Folder to Preparation")
                    : tr("Add Tracks to Preparation"));
    m_pAddToAutoDJAction->setText(recursive
                    ? tr("Add Folder to Auto DJ Queue (bottom)")
                    : tr("Add Tracks to Auto DJ Queue (bottom)"));

    QMenu menu(m_pSidebarWidget);
    menu.addAction(m_pAnalyzeFolderAction.get());
    menu.addSeparator();
    menu.addAction(m_pAddToPreparationAction.get());
    menu.addAction(m_pAddToAutoDJAction.get());
    menu.addAction(m_pCreateCrateAction.get());
    menu.addSeparator();
    menu.addAction(m_pOpenInFileBrowserAction.get());
    menu.addSeparator();
    menu.addAction(m_pRefreshTreeAction.get());
    menu.addAction(m_pRescanLibraryAction.get());
    menu.exec(globalPos);
}

QList<TrackId> LibraryFolderFeature::trackIdsInFolder(const FolderRef& folder) const {
    QList<TrackId> trackIds;
    QSqlQuery query(m_pTrackCollection->database());
    query.setForwardOnly(true);
    query.prepare(QStringLiteral(
            "SELECT library.id FROM library "
            "INNER JOIN track_locations "
            "ON library.location=track_locations.id "
            "WHERE library.mixxx_deleted=0 "
            "AND track_locations.fs_deleted=0 "
            "AND %1 "
            "ORDER BY track_locations.directory, track_locations.filename")
                    .arg(LibraryFolderTableModel::folderConditionSql(
                            folder.path, folder.scope)));
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return trackIds;
    }
    while (query.next()) {
        trackIds.append(TrackId(query.value(0)));
    }
    return trackIds;
}

void LibraryFolderFeature::slotAnalyzeFolder() {
    if (!m_lastRightClickedFolder.isValid()) {
        return;
    }
    const QList<TrackId> trackIds = trackIdsInFolder(m_lastRightClickedFolder);
    if (trackIds.isEmpty()) {
        return;
    }
    QList<AnalyzerScheduledTrack> tracks;
    tracks.reserve(trackIds.size());
    for (const TrackId& trackId : trackIds) {
        tracks.append(trackId);
    }
    emit analyzeTracks(tracks);
}

void LibraryFolderFeature::slotAddFolderToPreparation() {
    if (!m_lastRightClickedFolder.isValid()) {
        return;
    }
    mixxx::preparation::appendTracks(m_pConfig,
            m_pTrackCollection->getPlaylistDAO(),
            trackIdsInFolder(m_lastRightClickedFolder));
}

void LibraryFolderFeature::slotCreateCrateFromFolder() {
    if (!m_lastRightClickedFolder.isValid()) {
        return;
    }
    const QList<TrackId> trackIds = trackIdsInFolder(m_lastRightClickedFolder);

    const QString baseName = folderDisplayName(m_lastRightClickedFolder);
    QString crateName = baseName;
    for (int i = 2; m_pTrackCollection->crates().readCrateByName(crateName); ++i) {
        crateName = QStringLiteral("%1 (%2)").arg(baseName, QString::number(i));
    }

    Crate newCrate;
    newCrate.setName(crateName);
    CrateId newCrateId;
    if (!m_pTrackCollection->insertCrate(newCrate, &newCrateId)) {
        qWarning() << "Failed to create crate" << crateName
                   << "from folder" << m_lastRightClickedFolder.path;
        return;
    }
    if (!trackIds.isEmpty() &&
            !m_pTrackCollection->addCrateTracks(newCrateId, trackIds)) {
        qWarning() << "Failed to add" << trackIds.size()
                   << "tracks to crate" << crateName;
    }
}

void LibraryFolderFeature::slotAddFolderToAutoDJ() {
    if (!m_lastRightClickedFolder.isValid()) {
        return;
    }
    const QList<TrackId> trackIds = trackIdsInFolder(m_lastRightClickedFolder);
    if (trackIds.isEmpty()) {
        return;
    }
    m_pTrackCollection->getPlaylistDAO().addTracksToAutoDJQueue(
            trackIds, PlaylistDAO::AutoDJSendLoc::BOTTOM);
}

void LibraryFolderFeature::slotOpenInFileBrowser() {
    if (!m_lastRightClickedFolder.isValid()) {
        return;
    }
    mixxx::DesktopHelper::openInFileBrowser(QStringList{m_lastRightClickedFolder.path});
}

void LibraryFolderFeature::slotRescanLibrary() {
    m_pLibrary->trackCollectionManager()->startLibraryScan();
}
