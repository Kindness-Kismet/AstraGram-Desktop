/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/launcher_mac.h"

#include "core/crash_reports.h"
#include "core/update_checker.h"
#include "base/base_file_utilities.h"
#include "base/platform/base_platform_file_utilities.h"
#include "base/platform/mac/base_utilities_mac.h"

#ifdef _DEBUG
#include "extras/debug/window_focus.h"
#endif

#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>
#include <sys/sysctl.h>

namespace Platform {

Launcher::Launcher(int argc, char *argv[])
: Core::Launcher(argc, argv) {
}

void Launcher::initHook() {
	base::RegisterBundledResources(u"Telegram.rcc"_q);
}

bool Launcher::launchUpdater(UpdaterLaunch action) {
	if (cExeName().isEmpty()) {
		return false;
	}
	// WHY: every install path ends here after the app has quit, so the kept
	// package is verified and unpacked only now, and a package that fails
	// brings the current version back instead of leaving the user without it.
	if (action == UpdaterLaunch::PerformUpdate
		&& Core::prepareReadyUpdate() != Core::ReadyUpdateInstall::Unpacked) {
		action = UpdaterLaunch::JustRelaunch;
	}
	@autoreleasepool {

	auto passWorkingDir = customWorkingDir();
#ifdef _DEBUG
	NSMutableArray *debugArguments = [NSMutableArray array];
	if (cDebugProfile()) {
		[debugArguments addObject:@"-debugprofile"];
		passWorkingDir = true;
	}
	if (cDebugOffline()) {
		[debugArguments addObject:@"-debugoffline"];
	}
	const auto noFocus = ExtrasDebug::WindowFocus::noFocus();
	if (noFocus) {
		[debugArguments addObject:@"--no-focus"];
	}
#endif

	if (action == UpdaterLaunch::JustRelaunch) {
#ifdef OS_MAC_STORE
		const auto updaterDisabled = true;
#else
		const auto updaterDisabled = Core::UpdaterDisabled();
#endif
		if (updaterDisabled) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
			NSMutableArray *args = [NSMutableArray array];
			auto options = NSWorkspaceLaunchAsync | NSWorkspaceLaunchNewInstance;
#ifdef _DEBUG
			[args addObjectsFromArray:debugArguments];
			if (cDebugProfile()) {
				[args addObject:@"-workdir"];
				[args addObject:Q2NSString(cWorkingDir())];
			}
			if (noFocus) {
				options |= NSWorkspaceLaunchWithoutActivation;
			}
#endif
			NSDictionary *conf = [NSDictionary
				dictionaryWithObject:args
				forKey:NSWorkspaceLaunchConfigurationArguments];
			[[NSWorkspace sharedWorkspace]
				launchApplicationAtURL:[NSURL fileURLWithPath:Q2NSString(cExeDir() + cExeName())]
				options:options
				configuration:conf
				error:0];
#pragma clang diagnostic pop
			return true;
		}
	}

	NSString *path = @"", *args = @"";
	@try {
		path = [[NSBundle mainBundle] bundlePath];
		if (!path) {
			LOG(("Could not get bundle path!!"));
			return false;
		}
		path = [path stringByAppendingString:@"/Contents/Frameworks/Updater"];
		base::Platform::RemoveQuarantine(QFile::decodeName([path fileSystemRepresentation]));

		NSMutableArray *args = [[NSMutableArray alloc] initWithObjects:@"-workpath", Q2NSString(cWorkingDir()), @"-procid", nil];
		[args addObject:[NSString stringWithFormat:@"%d", [[NSProcessInfo processInfo] processIdentifier]]];
		if (cRestartingToSettings()) [args addObject:@"-tosettings"];
		if (action == UpdaterLaunch::JustRelaunch) [args addObject:@"-noupdate"];
		if (cLaunchMode() == LaunchModeAutoStart) [args addObject:@"-autostart"];
		if (Logs::DebugEnabled()) [args addObject:@"-debug"];
#ifdef _DEBUG
		[args addObjectsFromArray:debugArguments];
#endif
		if (cStartInTray()) [args addObject:@"-startintray"];
		if (cDataFile() != u"data"_q) {
			[args addObject:@"-key"];
			[args addObject:Q2NSString(cDataFile())];
		}
		if (passWorkingDir) {
			[args addObject:@"-workdir_custom"];
		}

		DEBUG_LOG(("Application Info: executing %1 %2").arg(NS2QString(path)).arg(NS2QString([args componentsJoinedByString:@" "])));
		Logs::closeMain();
		CrashReports::Finish();
		if (![NSTask launchedTaskWithLaunchPath:path arguments:args]) {
			DEBUG_LOG(("Task not launched while executing %1 %2").arg(NS2QString(path)).arg(NS2QString([args componentsJoinedByString:@" "])));
			return false;
		}
	}
	@catch (NSException *exception) {
		LOG(("Exception caught while executing %1 %2").arg(NS2QString(path)).arg(NS2QString(args)));
		return false;
	}
	@finally {
	}

	}
	return true;
}

} // namespace Platform
