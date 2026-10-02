#pragma once

#include "extras/data/entities.h"
#include "base/bytes.h"

#include <functional>

class SchemaVersion
{
public:
	int id;
	int version;
};

namespace Database {

void initialize();
void unlockMessages(bytes::const_span localKey);
[[nodiscard]] bool hasProtectedMessages();
[[nodiscard]] bool messageArchiveReady();
[[nodiscard]] QString messageArchiveError();
[[nodiscard]] std::vector<ExtrasMessageBase> getArchivedMessages(
	bool edited, ID userId, ID dialogId, ID topicId, ID messageId,
	ID minId, ID maxId, int totalLimit, const std::string &searchQuery = {},
	QString *error = nullptr);

void addEditedMessage(const EditedMessage &message);
std::vector<EditedMessage> getEditedMessages(ID userId, ID dialogId, ID messageId, ID minId, ID maxId, int totalLimit);
bool hasRevisions(ID userId, ID dialogId, ID messageId);

void addDeletedMessage(const DeletedMessage &message);
std::vector<ID> getDeletedMessageIds(ID userId, ID dialogId, ID topicId);
std::vector<DeletedMessage> getDeletedMessages(ID userId, ID dialogId, ID topicId, ID minId, ID maxId, int totalLimit, const std::string &searchQuery = "");
bool hasDeletedMessages(ID userId, ID dialogId, ID topicId);
void removeDeletedMessage(ID userId, ID dialogId, ID messageId);
void clearDeletedMessages(ID userId, ID dialogId, ID topicId);

std::vector<RegexFilter> getAllRegexFilters();
RegexFilter getById(std::vector<char> id);
std::vector<RegexFilter> getShared();
std::vector<RegexFilter> getByDialogId(ID dialogId);
std::vector<RegexFilterGlobalExclusion> getAllFiltersExclusions();
std::vector<RegexFilter> getExcludedByDialogId(ID dialogId);

int getCount();


void addRegexFilter(const RegexFilter &filter);
void addRegexExclusion(const RegexFilterGlobalExclusion &exclusion);

void updateRegexFilter(const RegexFilter &filter);

void deleteFilter(const std::vector<char> &id);
void deleteExclusionsByFilterId(const std::vector<char> &id);
void deleteExclusion(ID dialogId, std::vector<char> filterId);

void deleteAllFilters();
void deleteAllExclusions();

bool hasFilters();
bool hasPerDialogFilters();

}
