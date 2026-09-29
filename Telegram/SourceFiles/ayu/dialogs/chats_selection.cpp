// This file is part of AyuGram Desktop.
//
// For license and copyright information please follow this link:
// https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
#include "ayu/dialogs/chats_selection.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/notify/data_notify_settings.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

#include <ranges>

namespace Ayu {
namespace {

[[nodiscard]] ChatKind KindOfPeer(not_null<PeerData*> peer) {
	if (peer->isSelf()) {
		return ChatKind::Saved;
	} else if (peer->isBot()) {
		return ChatKind::Bot;
	} else if (peer->isUser()) {
		return ChatKind::Private;
	} else if (peer->isChat() || peer->isMegagroup()) {
		return ChatKind::Group;
	} else if (peer->isChannel()) {
		return ChatKind::Channel;
	}
	return ChatKind::Other;
}

[[nodiscard]] std::vector<not_null<PeerData*>> SelectedPeers(
		not_null<Window::SessionController*> controller) {
	auto result = std::vector<not_null<PeerData*>>();
	for (const auto id : ChatsSelection::instance().peers()) {
		if (const auto peer = controller->session().data().peerLoaded(id)) {
			result.push_back(peer);
		}
	}
	return result;
}

void ShowToast(
		not_null<Window::SessionController*> controller,
		const QString &text) {
	controller->showToast(text);
}

} // namespace

ChatsSelection &ChatsSelection::instance() {
	static ChatsSelection result;
	return result;
}

void ChatsSelection::begin(PeerId selfId) {
	if (_active && (_selfId == selfId)) {
		return;
	}
	_selfId = selfId;
	_active = true;
	_selected.clear();
	_syncedList = nullptr;
	fireChanged();
}

void ChatsSelection::end() {
	if (!_active) {
		return;
	}
	_active = false;
	_selected.clear();
	_candidates.clear();
	_syncedList = nullptr;
	_syncedSize = -1;
	_syncedHeight = -1;
	fireChanged();
}

void ChatsSelection::toggle(PeerId id) {
	if (!id) {
		return;
	}
	const auto i = _selected.find(id);
	if (i == _selected.end()) {
		_selected.emplace(id);
	} else {
		_selected.erase(i);
	}
	fireChanged();
}

void ChatsSelection::clearSelected() {
	if (_selected.empty()) {
		return;
	}
	_selected.clear();
	fireChanged();
}

void ChatsSelection::invert() {
	auto next = std::set<PeerId>();
	for (const auto &candidate : _candidates) {
		if (!contains(candidate.id)) {
			next.emplace(candidate.id);
		}
	}
	_selected = std::move(next);
	fireChanged();
}

void ChatsSelection::selectMatching(ChatPick pick) {
	for (const auto &candidate : _candidates) {
		if ((pick == ChatPick::All) || (pickOf(candidate.kind) == pick)) {
			_selected.emplace(candidate.id);
		}
	}
	fireChanged();
}

void ChatsSelection::syncCandidates(
		PeerId selfId,
		const Dialogs::IndexedList *list) {
	if (selfId != _selfId) {
		end();
		_selfId = selfId;
	}
	if (!_active) {
		return;
	} else if (!list) {
		end();
		return;
	}
	const auto refreshed = (list != _syncedList)
		|| (list->size() != _syncedSize)
		|| (list->height() != _syncedHeight);
	if (!refreshed) {
		return;
	}
	_syncedList = list;
	_syncedSize = list->size();
	_syncedHeight = list->height();
	auto seen = std::set<PeerId>();
	auto next = std::vector<ChatCandidate>();
	for (const auto &row : list->all()) {
		const auto history = row->key().history();
		if (!history) {
			continue;
		}
		const auto id = history->peer->id;
		if (!seen.emplace(id).second) {
			continue;
		}
		next.push_back(ChatCandidate{ id, KindOfPeer(history->peer.get()) });
	}
	_candidates = std::move(next);
	pruneSelected();
	fireChanged();
}

ChatPick ChatsSelection::pickOf(ChatKind kind) {
	switch (kind) {
	case ChatKind::Saved:
	case ChatKind::Private: return ChatPick::Private;
	case ChatKind::Bot: return ChatPick::Bots;
	case ChatKind::Group: return ChatPick::Groups;
	case ChatKind::Channel: return ChatPick::Channels;
	}
	return ChatPick::All;
}

void ChatsSelection::pruneSelected() {
	auto kept = std::set<PeerId>();
	for (const auto &candidate : _candidates) {
		if (contains(candidate.id)) {
			kept.emplace(candidate.id);
		}
	}
	_selected = std::move(kept);
}

void ChatsSelection::fireChanged() {
	_changes.fire({});
}

void BulkMute(const ChatsBulk &data, bool mute) {
	const auto notifySettings = &data.controller->session().data()
		.notifySettings();
	for (const auto peer : SelectedPeers(data.controller)) {
		if (peer->isSelf()) {
			continue;
		}
		notifySettings->update(
			peer,
			(mute
				? Data::MuteValue{ .forever = true }
				: Data::MuteValue{ .unmute = true }));
	}
	ShowToast(data.controller, mute
		? tr::lng_quick_dialog_action_toast_mute_success(tr::now)
		: tr::lng_quick_dialog_action_toast_unmute_success(tr::now));
}

void BulkArchive(const ChatsBulk &data, bool archive) {
	const auto show = data.controller->uiShow();
	for (const auto peer : SelectedPeers(data.controller)) {
		const auto history = peer->owner().history(peer);
		if (Window::IsArchived(history) == archive) {
			continue;
		} else if (!Window::CanArchive(history, peer)) {
			continue;
		}
		Window::ToggleHistoryArchived(show, history, archive);
	}
}

void BulkMarkRead(const ChatsBulk &data) {
	for (const auto peer : SelectedPeers(data.controller)) {
		Window::MarkAsReadThread(peer->owner().history(peer));
	}
	ShowToast(
		data.controller,
		tr::lng_quick_dialog_action_toast_read_success(tr::now));
}

void BulkMarkUnread(const ChatsBulk &data) {
	for (const auto peer : SelectedPeers(data.controller)) {
		peer->owner().histories().changeDialogUnreadMark(
			peer->owner().history(peer),
			true);
	}
}

void BulkPin(const ChatsBulk &data, bool pin) {
	for (const auto peer : SelectedPeers(data.controller)) {
		const auto entry = (Dialogs::Entry*)(peer->owner().history(peer));
		if (entry->isPinnedDialog(data.filterId) == pin) {
			continue;
		}
		Window::TogglePinnedThread(
			data.controller,
			entry,
			data.filterId,
			nullptr);
	}
}

void BulkClearBotsHistory(const ChatsBulk &data) {
	auto cleared = 0;
	for (const auto peer : SelectedPeers(data.controller)) {
		if (!peer->isBot()) {
			continue;
		}
		peer->session().api().clearHistory(peer, false);
		++cleared;
	}
	ShowToast(
		data.controller,
		tr::ayu_ChatBotsCleared(tr::now, lt_count, cleared));
}

void BulkDelete(const ChatsBulk &data) {
	const auto controller = data.controller;
	const auto peers = SelectedPeers(controller);
	if (peers.empty()) {
		ShowToast(controller, tr::ayu_ChatNothingSelected(tr::now));
		return;
	}
	const auto remove = [=](bool withSaved) {
		auto skipped = 0;
		for (const auto peer : peers) {
			if (peer->isSelf()) {
				if (withSaved) {
					peer->session().api().clearHistory(peer, false);
				}
				continue;
			}
			const auto channel = peer->asChannel();
			if (channel && channel->amCreator()) {
				++skipped;
				continue;
			}
			peer->session().api().deleteConversation(peer, false);
		}
		if (skipped) {
			ShowToast(
				controller,
				tr::ayu_ChatDeleteSkipped(tr::now, lt_count, skipped));
		}
		ChatsSelection::instance().end();
	};
	const auto yes = tr::lng_box_yes(tr::now);
	const auto no = tr::lng_box_no(tr::now);
	const auto hasSaved = std::ranges::any_of(
		peers,
		[](not_null<PeerData*> peer) { return peer->isSelf(); });
	if (!hasSaved) {
		controller->show(Ui::MakeConfirmBox({
			.text = tr::ayu_ChatDeleteConfirm(
				tr::now,
				lt_count,
				static_cast<int>(peers.size())),
			.confirmed = [=](Fn<void()> &&close) {
				close();
				remove(false);
			},
			.confirmText = tr::lng_profile_delete_conversation(tr::now),
			.cancelText = tr::lng_cancel(tr::now),
		}));
		return;
	}
	controller->show(Ui::MakeConfirmBox({
		.text = tr::ayu_ChatDeleteSavedAsk(tr::now),
		.confirmed = [=](Fn<void()> &&close) {
			close();
			controller->show(Ui::MakeConfirmBox({
				.text = tr::ayu_ChatDeleteSavedWarn(tr::now),
				.confirmed = [=](Fn<void()> &&inner) {
					inner();
					remove(true);
				},
				.cancelled = [=] { remove(false); },
				.confirmText = yes,
				.cancelText = no,
			}));
		},
		.cancelled = [=] { remove(false); },
		.confirmText = yes,
		.cancelText = no,
	}));
}

} // namespace Ayu
