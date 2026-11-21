#include "library/folders/libraryfoldertablemodel.h"

#include <QCryptographicHash>

#include "library/dao/trackschema.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "mixer/playerinfo.h"
#include "moc_libraryfoldertablemodel.cpp"
#include "util/db/fwdsqlquery.h"

namespace {

const QString kModelName = QStringLiteral("libraryfolder");

QString sqlStringLiteral(QString value) {
    value.replace(QChar('\''), QStringLiteral("''"));
    return QChar('\'') + value + QChar('\'');
}

} // anonymous namespace

LibraryFolderTableModel::LibraryFolderTableModel(
        QObject* pParent,
        TrackCollectionManager* pTrackCollectionManager)
        : BaseSqlTableModel(
                  pParent,
                  pTrackCollectionManager,
                  // Same settings as LibraryTableModel (the "Tracks" view):
                  // the folder view uses the same columns, widths, order and
                  // sorting. The header state is stored by column name.
                  "mixxx.db.model.library"),
          m_scope(Scope::Recursive),
          m_hasSelection(false) {
}

// static
QString LibraryFolderTableModel::folderConditionSql(
        const QString& folderPath, Scope scope) {
    if (scope == Scope::AllTracks) {
        return QStringLiteral("1");
    }
    QString folder = folderPath;
    while (folder.size() > 1 && folder.endsWith(QChar('/'))) {
        folder.chop(1);
    }
    if (scope == Scope::FolderOnly) {
        return QStringLiteral("(track_locations.directory=%1)")
                .arg(sqlStringLiteral(folder));
    }
    const QString prefix = folder.endsWith(QChar('/')) ? folder : folder + QChar('/');
    // substr() instead of LIKE avoids having to escape '%' and '_',
    // which are common in file names. SQLite counts characters (code
    // points), not UTF-16 code units, so emojis etc. count as one.
    return QStringLiteral(
            "(track_locations.directory=%1 OR "
            "substr(track_locations.directory,1,%2)=%3)")
            .arg(sqlStringLiteral(folder),
                    QString::number(prefix.toUcs4().size()),
                    sqlStringLiteral(prefix));
}

QString LibraryFolderTableModel::selectionKey() const {
    return QString::number(static_cast<int>(m_scope)) + QChar(':') + m_selectedFolder;
}

void LibraryFolderTableModel::selectFolder(const QString& folderPath, Scope scope) {
    if (m_hasSelection && folderPath == m_selectedFolder && scope == m_scope) {
        return;
    }
    // Store search text of the previous folder
    if (m_hasSelection) {
        const QString currSearch = currentSearch();
        if (!currSearch.trimmed().isEmpty()) {
            m_searchTexts.insert(selectionKey(), currSearch);
        } else {
            m_searchTexts.remove(selectionKey());
        }
    }

    m_selectedFolder = folderPath;
    m_scope = scope;
    m_hasSelection = true;

    const QString tableName = QStringLiteral("libraryfolder_") +
            QString::fromLatin1(QCryptographicHash::hash(
                    selectionKey().toUtf8(), QCryptographicHash::Md5)
                                        .toHex());

    QStringList columns;
    columns << QStringLiteral("library.") + LIBRARYTABLE_ID
            << "'' AS " + LIBRARYTABLE_PREVIEW
            // For sorting the cover art column we give LIBRARYTABLE_COVERART
            // the same value as the cover digest.
            << LIBRARYTABLE_COVERART_DIGEST + " AS " + LIBRARYTABLE_COVERART;
    const QString queryString =
            QStringLiteral(
                    "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS "
                    "SELECT %2 FROM library "
                    "INNER JOIN track_locations "
                    "ON library.location=track_locations.id "
                    "WHERE library.mixxx_deleted=0 "
                    "AND track_locations.fs_deleted=0 "
                    "AND %3")
                    .arg(tableName,
                            columns.join(QChar(',')),
                            folderConditionSql(folderPath, scope));
    FwdSqlQuery(m_database, queryString).execPrepared();

    QStringList tableColumns;
    tableColumns << LIBRARYTABLE_ID
                 << LIBRARYTABLE_PREVIEW
                 << LIBRARYTABLE_COVERART;
    setTable(tableName,
            LIBRARYTABLE_ID,
            std::move(tableColumns),
            m_pTrackCollectionManager->internalCollection()->getTrackSource());

    // Restore search text
    setSearch(m_searchTexts.value(selectionKey()));
    setDefaultSort(fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ARTIST), Qt::AscendingOrder);
}

bool LibraryFolderTableModel::isColumnInternal(int column) {
    return column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ID) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_URL) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_CUEPOINT) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_WAVESUMMARYHEX) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_SAMPLERATE) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_MIXXXDELETED) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_HEADERPARSED) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_PLAYED) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_KEY_ID) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_BPM_LOCK) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_CHANNELS) ||
            column == fieldIndex(ColumnCache::COLUMN_TRACKLOCATIONSTABLE_FSDELETED) ||
            (PlayerInfo::instance().numPreviewDecks() == 0 &&
                    column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_PREVIEW)) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART_SOURCE) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART_TYPE) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART_LOCATION) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART_COLOR) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART_DIGEST) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART_HASH);
}

TrackModel::Capabilities LibraryFolderTableModel::getCapabilities() const {
    return Capability::AddToTrackSet |
            Capability::AddToAutoDJ |
            Capability::EditMetadata |
            Capability::LoadToDeck |
            Capability::LoadToSampler |
            Capability::LoadToPreviewDeck |
            Capability::Hide |
            Capability::ResetPlayed |
            Capability::RemoveFromDisk |
            Capability::Analyze |
            Capability::Properties |
            Capability::Sorting;
}

QString LibraryFolderTableModel::modelKey(bool noSearch) const {
    if (noSearch) {
        return kModelName + QChar(':') + selectionKey();
    }
    return kModelName + QChar(':') + selectionKey() + QChar('#') + currentSearch();
}
