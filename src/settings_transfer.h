#pragma once

#include <string>

// Settings travel as one ZIP archive: OpenChatMix.ini with the Presets and PluginStates folders beside it.

// Writes the settings files of configDirectory to zipPath; replaces a file already there.
bool ExportSettings(const std::wstring& configDirectory, const std::wstring& zipPath);

// Whether a file is a settings archive: OpenChatMix.ini with a [ChatMix] section, stored without
// compression, and only safe paths inside the settings folders.
bool IsSettingsArchive(const std::wstring& zipPath);

// Copies the settings files of one folder into another, replacing files of the same name; the ini comes last,
// as it marks a portable folder. False if one could not be copied.
bool CopySettings(const std::wstring& from, const std::wstring& to);

// Sends the settings files of a folder to the Recycle Bin; false if the ini is still there.
bool RecycleSettings(const std::wstring& directory);

// Where an archive waits to be imported by the next start of OpenChatMix.
std::wstring PendingImportPath(const std::wstring& configDirectory);

// configDirectory\Backups: the copies made before an import, and where export and import open first.
std::wstring BackupDirectory(const std::wstring& configDirectory);

// Puts the archive's settings in configDirectory: the ini is replaced, presets and plugin states of the
// same name are overwritten and others are kept. The current settings are first exported to
// configDirectory\Backups. False if the archive is not a settings archive or the backup failed; nothing
// changes then.
bool ImportSettings(const std::wstring& configDirectory, const std::wstring& zipPath);
