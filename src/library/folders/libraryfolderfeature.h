#pragma once

#include <QAction>
#include <QList>
#include <QModelIndex>
#include <QPointer>
#include <QString>

#include "library/folders/libraryfoldertablemodel.h"
#include "library/libraryfeature.h"
#include "library/treeitemmodel.h"
#include "track/trackid.h"
#include "util/parented_ptr.h"

class AnalyzerScheduledTrack;
class TrackCollection;
class WLibrarySidebar;

/// Sidebar feature "Folders": browse the tracks of the library by the
/// directory structure on disk. Unlike "Computer" the data comes from the
/// library database, so selecting a folder shows the tracks of the folder
/// and all its subfolders, including all analysis results.
///
/// The library root directories themselves are not shown, their
/// subfolders are the top level items.
class LibraryFolderFeature final : public LibraryFeature {
    Q_OBJECT

  public:
    LibraryFolderFeature(Library* pLibrary, UserSettingsPointer pConfig);
    ~LibraryFolderFeature() override = default;

    QVariant title() override;
    TreeItemModel* sidebarModel() const override;

    void bindSidebarWidget(WLibrarySidebar* pSidebarWidget) override;

    bool hasTrackTable() override {
        return true;
    }

  signals:
    void analyzeTracks(const QList<AnalyzerScheduledTrack>& tracks);

  public slots:
    void activate() override;
    void activateChild(const QModelIndex& index) override;
    void onRightClick(const QPoint& globalPos) override;
    void onRightClickChild(const QPoint& globalPos, const QModelIndex& index) override;

    void slotRebuildFolderTree();

  private slots:
    void slotAnalyzeFolder();
    void slotAddFolderToPreparation();
    void slotCreateCrateFromFolder();
    void slotAddFolderToAutoDJ();
    void slotOpenInFileBrowser();
    void slotRescanLibrary();

  private:
    struct FolderRef {
        QString path;
        LibraryFolderTableModel::Scope scope = LibraryFolderTableModel::Scope::Recursive;

        bool isValid() const {
            return !path.isEmpty();
        }
    };

    static FolderRef folderRefFromIndex(const QModelIndex& index);
    QList<TrackId> trackIdsInFolder(const FolderRef& folder) const;
    QString folderDisplayName(const FolderRef& folder) const;

    TrackCollection* const m_pTrackCollection;

    parented_ptr<TreeItemModel> m_pSidebarModel;
    LibraryFolderTableModel m_folderTableModel;

    parented_ptr<QAction> m_pAnalyzeFolderAction;
    parented_ptr<QAction> m_pAddToPreparationAction;
    parented_ptr<QAction> m_pCreateCrateAction;
    parented_ptr<QAction> m_pAddToAutoDJAction;
    parented_ptr<QAction> m_pOpenInFileBrowserAction;
    parented_ptr<QAction> m_pRescanLibraryAction;
    parented_ptr<QAction> m_pRefreshTreeAction;

    // The folder of the last right-clicked sidebar item. Not a QModelIndex,
    // because the tree may be rebuilt while the context menu is open.
    FolderRef m_lastRightClickedFolder;

    QPointer<WLibrarySidebar> m_pSidebarWidget;
};
