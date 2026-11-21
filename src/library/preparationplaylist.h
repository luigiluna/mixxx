#pragma once

#include <QList>

#include "preferences/usersettings.h"
#include "track/trackid.h"

class PlaylistDAO;

/// Helpers for the playlist shown in the preparation panel
/// (see WPreparationPanel). Shared by the panel and by the sidebar
/// features that offer "Add to Preparation".
namespace mixxx::preparation {

/// Config key that stores the id of the playlist shown in the panel.
ConfigKey playlistIdConfigKey();

/// Returns the id of the playlist shown in the preparation panel,
/// creating the default "Preparation" playlist if necessary.
/// Returns kInvalidPlaylistId (-1) on failure.
int ensurePlaylist(const UserSettingsPointer& pConfig, PlaylistDAO& playlistDao);

/// Appends the tracks to the preparation playlist and shows the panel.
/// Returns false if nothing has been added.
bool appendTracks(const UserSettingsPointer& pConfig,
        PlaylistDAO& playlistDao,
        const QList<TrackId>& trackIds);

} // namespace mixxx::preparation
