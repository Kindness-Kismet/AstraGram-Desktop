/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_resources.h"

#include "twidget/resources.h"

namespace Tgw {
namespace {

enum class Status : uchar {
	Idle,
	Preparing,
	Ready,
	Failed,
};

struct Script {
	QString name;
	QByteArray source;
};

struct Package {
	Script sandbox;
	Script validator;
};

struct State {
	Status status = Status::Idle;
	std::shared_ptr<twidget::SandboxResources> resources;
	std::vector<Fn<void(std::shared_ptr<twidget::SandboxResources>)>> waiters;
	Fn<void()> stopped;
};

[[nodiscard]] State &Current() {
	static auto result = State();
	return result;
}

[[nodiscard]] QByteArray ReadAsset(const QString &name) {
	auto file = QFile(u":/tgw/"_q + name);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

[[nodiscard]] QByteArray ReadSource(const QString &name) {
	auto result = ReadAsset(name);
	result.replace("\r\n", "\n");
	return result;
}

[[nodiscard]] std::optional<Script> ReadScript(const QString &name) {
	auto result = Script{
		.name = name,
		.source = ReadSource(name + u".js"_q),
	};
	if (result.source.isEmpty()) {
		LOG(("Tgw Error: Missing packaged '%1.js'.").arg(name));
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] std::optional<Package> ReadPackage() {
	auto sandbox = ReadScript(u"sandbox-bundle"_q);
	auto validator = ReadScript(u"validator-bundle"_q);
	if (!sandbox || !validator) {
		return std::nullopt;
	}
	return Package{
		.sandbox = std::move(*sandbox),
		.validator = std::move(*validator),
	};
}

[[nodiscard]] std::shared_ptr<twidget::SandboxResources> Prepare() {
	const auto package = ReadPackage();
	if (!package) {
		return nullptr;
	}
	// 内置脚本直接交给沙箱，避免文件加载接口的异常和重复写入。
	auto result = std::make_shared<twidget::SandboxResources>();
	result->sandboxBundle = package->sandbox.source.toStdString();
	result->validatorBundle = package->validator.source.toStdString();
	return result;
}

void Finish(std::shared_ptr<twidget::SandboxResources> resources) {
	auto &state = Current();
	state.status = resources ? Status::Ready : Status::Failed;
	state.resources = resources;
	for (const auto &done : base::take(state.waiters)) {
		done(resources);
	}
	if (const auto stopped = base::take(state.stopped)) {
		stopped();
	}
}

} // namespace

void PrepareResources(
		Fn<void(std::shared_ptr<twidget::SandboxResources>)> done) {
	auto &state = Current();
	if (state.status == Status::Ready || state.status == Status::Failed) {
		const auto resources = state.resources;
		crl::on_main([done = std::move(done), resources] {
			done(resources);
		});
		return;
	}
	state.waiters.push_back(std::move(done));
	if (state.status == Status::Preparing) {
		return;
	}
	state.status = Status::Preparing;
	crl::async([=] {
		auto resources = Prepare();
		crl::on_main([resources = std::move(resources)] {
			Finish(resources);
		});
	});
}

bool StopResources(Fn<void()> stopped) {
	auto &state = Current();
	state.waiters.clear();
	if (state.status != Status::Preparing) {
		return false;
	}
	state.stopped = std::move(stopped);
	return true;
}

bool ResourcesFailed() {
	return (Current().status == Status::Failed);
}

} // namespace Tgw
