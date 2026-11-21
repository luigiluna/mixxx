#pragma once

#include <QWidget>

#include "preferences/usersettings.h"
#include "widget/wbasewidget.h"

class KeyboardEventFilter;
class Library;
class PlaylistTableModel;
class QComboBox;
class WTrackTableView;

/// A second, independent track table next to the main library view that
/// always shows one playlist (by default "Preparation"), like the Prepare
/// panel in Serato or the Sidelist in VirtualDJ. Tracks can be dragged
/// into it from the main library table and loaded to decks from it.
class WPreparationPanel : public QWidget, public WBaseWidget {
    Q_OBJECT

  public:
    WPreparationPanel(QWidget* pParent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            KeyboardEventFilter* pKeyboard,
            double trackTableBackgroundColorOpacity);

  protected:
    void showEvent(QShowEvent* pEvent) override;

  private slots:
    void slotPlaylistsChanged();
    void slotPlaylistDeleted(int playlistId);
    void slotComboBoxActivated(int index);

  private:
    void ensurePlaylistSelected();
    void selectPlaylist(int playlistId);
    void populateComboBox();
    void initDefaultColumns();

    const UserSettingsPointer m_pConfig;
    Library* const m_pLibrary;

    QComboBox* m_pComboBox;
    WTrackTableView* m_pTrackTableView;
    PlaylistTableModel* m_pPlaylistTableModel;
};
