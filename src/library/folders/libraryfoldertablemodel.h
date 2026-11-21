#pragma once

#include <QHash>
#include <QString>

#include "library/basesqltablemodel.h"

/// Shows library tracks by folder. The contents are read from the library
/// database, so all metadata columns (BPM, key, rating, cover, ...) are
/// available. The column layout is shared with the main "Tracks" table.
class LibraryFolderTableModel final : public BaseSqlTableModel {
    Q_OBJECT

  public:
    enum class Scope {
        /// All tracks of the library
        AllTracks,
        /// Tracks in the folder and all its subfolders
        Recursive,
        /// Only the tracks located directly in the folder
        FolderOnly,
    };

    LibraryFolderTableModel(QObject* parent,
            TrackCollectionManager* pTrackCollectionManager);
    ~LibraryFolderTableModel() final = default;

    void selectFolder(const QString& folderPath, Scope scope);

    bool isColumnInternal(int column) final;
    Capabilities getCapabilities() const final;
    QString modelKey(bool noSearch) const final;

    /// Returns an SQL condition on track_locations for the given folder.
    /// folderPath is embedded as an escaped literal, because SQLite views
    /// do not support bound parameters.
    static QString folderConditionSql(const QString& folderPath, Scope scope);

  private:
    QString selectionKey() const;

    QString m_selectedFolder;
    Scope m_scope;
    bool m_hasSelection;
    QHash<QString, QString> m_searchTexts;
};
