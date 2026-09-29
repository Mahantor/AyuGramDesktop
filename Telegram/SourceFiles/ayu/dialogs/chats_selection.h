// This file is part of AyuGram Desktop.
//
// For license and copyright information please follow this link:
// https://github.com/AyuGram/AyuGramDesktop/blob/dev/LICENSE
#pragma once

#include "mtproto/core_types.h"
#include "data/data_peer_id.h"

#include <rpl/event_stream.h>
#include <rpl/producer.h>

#include <set>
#include <vector>

namespace Dialogs {
class IndexedList;
} // namespace Dialogs

namespace Window {
class SessionController;
} // namespace Window

namespace Ayu {

enum class ChatPick : uchar {
	All,
	Private,
	Bots,
	Groups,
	Channels,
};

enum class ChatKind : uchar {
	Other,
	Saved,
	Private,
	Bot,
	Group,
	Channel,
};

struct ChatCandidate {
	PeerId id;
	ChatKind kind = ChatKind::Other;
};

// PeerId-keyed so that nothing from a finished session can be reused.
class ChatsSelection {
public:
	[[nodiscard]] static ChatsSelection &instance();

	[[nodiscard]] PeerId selfId() const {
		return _selfId;
	}
	[[nodiscard]] bool active() const {
		return _active;
	}
	[[nodiscard]] int count() const {
		return static_cast<int>(_selected.size());
	}
	[[nodiscard]] bool contains(PeerId id) const {
		return _selected.find(id) != _selected.end();
	}
	[[nodiscard]] std::vector<PeerId> peers() const {
		return { std::begin(_selected), std::end(_selected) };
	}
	[[nodiscard]] rpl::producer<> changes() const {
		return _changes.events();
	}

	void begin(PeerId selfId);
	void end();
	void toggle(PeerId id);
	void clearSelected();
	void invert();
	void selectMatching(ChatPick pick);

	// Called from the chats list paint with the list it currently shows.
	void syncCandidates(PeerId selfId, const Dialogs::IndexedList *list);

private:
	[[nodiscard]] static ChatPick pickOf(ChatKind kind);

	void fireChanged();
	void pruneSelected();

	PeerId _selfId;
	bool _active = false;
	const Dialogs::IndexedList *_syncedList = nullptr;
	int _syncedSize = -1;
	int _syncedHeight = -1;
	std::vector<ChatCandidate> _candidates;
	std::set<PeerId> _selected;
	rpl::event_stream<> _changes;

};

struct ChatsBulk {
	not_null<Window::SessionController*> controller;
	int filterId = 0;
};

void BulkMute(const ChatsBulk &data, bool mute);
void BulkArchive(const ChatsBulk &data, bool archive);
void BulkMarkRead(const ChatsBulk &data);
void BulkMarkUnread(const ChatsBulk &data);
void BulkPin(const ChatsBulk &data, bool pin);
void BulkClearBotsHistory(const ChatsBulk &data);
void BulkDelete(const ChatsBulk &data);

} // namespace Ayu
