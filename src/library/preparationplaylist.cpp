#include "library/preparationplaylist.h"

#include <QtDebug>

#include "control/controlobject.h"
#include "library/dao/playlistdao.h"

namespace {

// Only used when the playlist is created. It can be renamed afterwards.
const QString kDefaultPlaylistName = QStringLiteral("Preparation");

// Created by the LateNight skin, see res/skins/LateNight/skin.xml
const ConfigKey kShowPanelConfigKey("[Skin]", "show_preparation_panel");

bool isUsablePlaylist(const PlaylistDAO& playlistDao, int playlistId) {
    return playlistId != kInvalidPlaylistId &&
            playlistDao.playlistExists(playlistId) &&
            playlistDao.getHiddenType(playlistId) == PlaylistDAO::PLHT_NOT_HIDDEN;
}

} // anonymous namespace

namespace mixxx::preparation {

ConfigKey playlistIdConfigKey() {
    return ConfigKey(QStringLiteral("[Library]"),
            QStringLiteral("PreparationPanelPlaylistId"));
}

int ensurePlaylist(const UserSettingsPointer& pConfig, PlaylistDAO& playlistDao) {
    int playlistId = pConfig->getValue(playlistIdConfigKey(), kInvalidPlaylistId);
    if (isUsablePlaylist(playlistDao, playlistId)) {
        return playlistId;
    }
    playlistId = playlistDao.getPlaylistIdFromName(kDefaultPlaylistName);
    if (!isUsablePlaylist(playlistDao, playlistId)) {
        QString name = kDefaultPlaylistName;
        playlistId = playlistDao.createUniquePlaylist(&name);
    }
    if (playlistId == kInvalidPlaylistId) {
        qWarning() << "Failed to create the preparation playlist";
        return kInvalidPlaylistId;
    }
    pConfig->setValue(playlistIdConfigKey(), playlistId);
    return playlistId;
}

bool appendTracks(const UserSettingsPointer& pConfig,
        PlaylistDAO& playlistDao,
        const QList<TrackId>& trackIds) {
    if (trackIds.isEmpty()) {
        return false;
    }
    const int playlistId = ensurePlaylist(pConfig, playlistDao);
    if (playlistId == kInvalidPlaylistId ||
            playlistDao.isPlaylistLocked(playlistId)) {
        return false;
    }
    if (!playlistDao.appendTracksToPlaylist(trackIds, playlistId)) {
        return false;
    }
    // Show the panel so the user sees where the tracks went. The control
    // only exists if the skin has a preparation panel.
    if (ControlObject::exists(kShowPanelConfigKey)) {
        ControlObject::set(kShowPanelConfigKey, 1.0);
    }
    return true;
}

} // namespace mixxx::preparation
