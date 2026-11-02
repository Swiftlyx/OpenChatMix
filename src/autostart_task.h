#pragma once

// Windows starts no program that needs administrator rights from the Run key at sign-in, so an elevated
// OpenChatMix starts through a Task Scheduler task with the highest privileges instead. Its logon trigger has no
// delay, while Explorer starts Run entries some seconds after the desktop appears.

// True while this process runs with administrator rights.
bool IsElevated();
// True if the current user's sign-in task of OpenChatMix exists and this process may see it.
bool AutostartTaskExists();
// Registers the task for the running executable (needs elevation), or removes it.
bool SetAutostartTask(bool enable);
