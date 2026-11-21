#include "widget/wpreparationpanel.h"

#include <QComboBox>
#include <QLabel>
#include <QSet>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/dao/playlistdao.h"
#include "library/library.h"
#include "library/playlisttablemodel.h"
#include "library/preparationplaylist.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_wpreparationpanel.cpp"
#include "widget/wtracktableview.h"
#include "widget/wtracktableviewheader.h"

namespace {

const ConfigKey kColumnsInitializedConfigKey("[Library]", "PreparationPanelColumnsInitialized");

PlaylistDAO& playlistDao(Library* pLibrary) {
    return pLibrary->trackCollectionManager()->internalCollection()->getPlaylistDAO();
}

} // anonymous namespace

WPreparationPanel::WPreparationPanel(QWidget* pParent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        KeyboardEventFilter* pKeyboard,
        double trackTableBackgroundColorOpacity)
        : QWidget(pParent),
          WBaseWidget(this),
          m_pConfig(pConfig),
          m_pLibrary(pLibrary),
          m_pComboBox(nullptr),
          m_pTrackTableView(nullptr),
          m_pPlaylistTableModel(nullptr) {
    m_pPlaylistTableModel = new PlaylistTableModel(this,
            pLibrary->trackCollectionManager(),
            "mixxx.db.model.preparation");

    auto* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->setSpacing(2);

    m_pComboBox = new QComboBox(this);
    // Reuse the object name of an existing combobox so that skins style
    // it without additional stylesheet rules (LateNight styles this name).
    m_pComboBox->setObjectName(QStringLiteral("fadeModeCombobox"));
    m_pComboBox->setToolTip(tr("Playlist shown in the preparation panel"));
    m_pComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_pComboBox->setMinimumContentsLength(8);
    // Don't steal keyboard focus from the library
    m_pComboBox->setFocusPolicy(Qt::ClickFocus);
    pLayout->addWidget(m_pComboBox);

    m_pTrackTableView = new WTrackTableView(this,
            pConfig,
            pLibrary,
            trackTableBackgroundColorOpacity);
    m_pTrackTableView->setObjectName(QStringLiteral("PreparationPanelTable"));
    // The panel always shows the set order (#) and must neither follow nor
    // change the sorting of the main library table.
    m_pTrackTableView->setSortingDisabled(true);
    // Accept tracks dragged from the main library table (or any other
    // track table). Dragging within the panel reorders the tracks.
    m_pTrackTableView->setAcceptDropsFromOtherTrackViews(true);
    if (pKeyboard) {
        m_pTrackTableView->installEventFilter(pKeyboard);
    }
    pLayout->addWidget(m_pTrackTableView, 1);

    connect(m_pComboBox,
            QOverload<int>::of(&QComboBox::activated),
            this,
            &WPreparationPanel::slotComboBoxActivated);

    connect(m_pTrackTableView,
            &WTrackTableView::loadTrack,
            pLibrary,
            &Library::slotLoadTrack);
    connect(m_pTrackTableView,
            &WTrackTableView::loadTrackToPlayer,
            pLibrary,
            &Library::slotLoadTrackToPlayer);
    connect(m_pTrackTableView,
            &WTrackTableView::trackSelected,
            pLibrary,
            &Library::trackSelected);

    // Keep font, row height and click behavior in sync with the main library
    connect(pLibrary,
            &Library::setTrackTableFont,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableFont);
    connect(pLibrary,
            &Library::setTrackTableRowHeight,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableRowHeight);
    connect(pLibrary,
            &Library::setSelectedClick,
            m_pTrackTableView,
            &WTrackTableView::setSelectedClick);
    m_pTrackTableView->setTrackTableFont(pLibrary->getTrackTableFont());
    m_pTrackTableView->setTrackTableRowHeight(pLibrary->getTrackTableRowHeight());
    m_pTrackTableView->setSelectedClick(pLibrary->selectedClickEnabled());

    PlaylistDAO& dao = playlistDao(pLibrary);
    connect(&dao, &PlaylistDAO::added, this, &WPreparationPanel::slotPlaylistsChanged);
    connect(&dao, &PlaylistDAO::renamed, this, &WPreparationPanel::slotPlaylistsChanged);
    connect(&dao, &PlaylistDAO::deleted, this, &WPreparationPanel::slotPlaylistDeleted);
}

void WPreparationPanel::showEvent(QShowEvent* pEvent) {
    QWidget::showEvent(pEvent);
    // Lazily create the default playlist when the panel is shown for the
    // first time, not already when the skin is loaded.
    ensurePlaylistSelected();
}

void WPreparationPanel::ensurePlaylistSelected() {
    PlaylistDAO& dao = playlistDao(m_pLibrary);
    const int playlistId = mixxx::preparation::ensurePlaylist(m_pConfig, dao);
    if (playlistId == kInvalidPlaylistId) {
        return;
    }
    if (playlistId != m_pPlaylistTableModel->getPlaylist()) {
        selectPlaylist(playlistId);
    }
}

void WPreparationPanel::selectPlaylist(int playlistId) {
    m_pConfig->setValue(mixxx::preparation::playlistIdConfigKey(), playlistId);
    m_pPlaylistTableModel->selectPlaylist(playlistId);
    // Always show the set order, so dragging within the panel reorders
    // the tracks like in a playlist.
    m_pPlaylistTableModel->sort(
            m_pPlaylistTableModel->fieldIndex(
                    ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_POSITION),
            Qt::AscendingOrder);
    m_pTrackTableView->loadTrackModel(m_pPlaylistTableModel);
    initDefaultColumns();
    populateComboBox();
}

void WPreparationPanel::initDefaultColumns() {
    // The panel is narrow, so initially only show the most important
    // columns. Afterwards the user can show/hide columns as usual via the
    // context menu of the header, which is then persisted.
    if (m_pConfig->getValue(kColumnsInitializedConfigKey, false)) {
        return;
    }
    auto* pHeader = qobject_cast<WTrackTableViewHeader*>(
            m_pTrackTableView->horizontalHeader());
    if (!pHeader || pHeader->hasPersistedHeaderState()) {
        m_pConfig->setValue(kColumnsInitializedConfigKey, true);
        return;
    }
    const QSet<int> visibleColumns = {
            m_pPlaylistTableModel->fieldIndex(
                    ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_POSITION),
            m_pPlaylistTableModel->fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_PREVIEW),
            m_pPlaylistTableModel->fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COLOR),
            m_pPlaylistTableModel->fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ARTIST),
            m_pPlaylistTableModel->fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_TITLE),
            m_pPlaylistTableModel->fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_BPM),
            m_pPlaylistTableModel->fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_KEY),
            m_pPlaylistTableModel->fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_DURATION),
    };
    for (int column = 0; column < pHeader->count(); ++column) {
        if (!visibleColumns.contains(column)) {
            pHeader->setSectionHidden(column, true);
        }
    }
    pHeader->saveHeaderState();
    m_pConfig->setValue(kColumnsInitializedConfigKey, true);
}

void WPreparationPanel::populateComboBox() {
    const QSignalBlocker blocker(m_pComboBox);
    m_pComboBox->clear();
    const QList<QPair<int, QString>> playlists =
            playlistDao(m_pLibrary).getPlaylists(PlaylistDAO::PLHT_NOT_HIDDEN);
    const int currentId = m_pPlaylistTableModel->getPlaylist();
    for (const auto& [id, name] : playlists) {
        m_pComboBox->addItem(name, id);
        if (id == currentId) {
            m_pComboBox->setCurrentIndex(m_pComboBox->count() - 1);
        }
    }
}

void WPreparationPanel::slotComboBoxActivated(int index) {
    bool ok = false;
    const int playlistId = m_pComboBox->itemData(index).toInt(&ok);
    if (!ok || playlistId == m_pPlaylistTableModel->getPlaylist()) {
        return;
    }
    selectPlaylist(playlistId);
}

void WPreparationPanel::slotPlaylistsChanged() {
    if (m_pPlaylistTableModel->getPlaylist() != kInvalidPlaylistId) {
        populateComboBox();
    }
}

void WPreparationPanel::slotPlaylistDeleted(int playlistId) {
    if (playlistId != m_pPlaylistTableModel->getPlaylist()) {
        slotPlaylistsChanged();
        return;
    }
    // The shown playlist has been deleted: fall back to the default one
    m_pConfig->setValue(mixxx::preparation::playlistIdConfigKey(), kInvalidPlaylistId);
    if (isVisible()) {
        ensurePlaylistSelected();
    } else {
        populateComboBox();
    }
}
