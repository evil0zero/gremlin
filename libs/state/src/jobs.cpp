#include "gremlin/state/jobs.hpp"

#include <sqlite3.h>
#include <array>

using namespace std;

namespace gremlin {
namespace {

Error db_error(sqlite3* db) {
    return {ErrorCode::internal, sqlite3_errmsg(db)};
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql) {
        sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr);
    }
    ~Statement() { sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    sqlite3_stmt* get() const { return statement_; }
    bool bind(int index, string_view value) const {
        return sqlite3_bind_text64(statement_, index, value.data(), value.size(),
                                   SQLITE_TRANSIENT, SQLITE_UTF8) == SQLITE_OK;
    }
private:
    sqlite3_stmt* statement_{};
};

bool execute(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

}

JobStore::~JobStore() { sqlite3_close(db_); }

Result<JobStore::Handle> JobStore::open(const filesystem::path& path) {
    sqlite3* db{};
    const auto utf8 = path.u8string();
    const int rc = sqlite3_open_v2(reinterpret_cast<const char*>(utf8.c_str()), &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    Handle store(new JobStore(db));
    if (!db) return Error{ErrorCode::internal, "SQLite could not allocate a connection"};
    if (rc != SQLITE_OK) return db_error(db);
    if (sqlite3_busy_timeout(db, 2000) != SQLITE_OK ||
        !execute(db, "PRAGMA synchronous=FULL; BEGIN IMMEDIATE;")) return db_error(db);

    int version = int();
    {
        Statement schema(db, "PRAGMA user_version");
        if (!schema.get() || sqlite3_step(schema.get()) != SQLITE_ROW) return db_error(db);
        version = sqlite3_column_int(schema.get(), 0);
    }
    if (version < 0 || version > 1) {
        return Error{ErrorCode::prerequisite, "unsupported job database schema version"};
    }
    if (version == 0 && !execute(db,
        "CREATE TABLE jobs(id TEXT PRIMARY KEY NOT NULL, original TEXT NOT NULL,"
        " state TEXT NOT NULL CHECK(state IN ('PENDING','RUNNING','VERIFIED','FAILED',"
        " 'CANCELLED','BLOCKED','UNCERTAIN'))); PRAGMA user_version=1;")) return db_error(db);
    if (!execute(db, "COMMIT;")) return db_error(db);
    return store;
}

JobStore::Status JobStore::submit(const JobId& id, string_view original) {
    if (original.empty() || holds_alternative<Error>(JobId::parse(id.value))) {
        return Error{ErrorCode::input, "expected a valid job ID and nonempty original request"};
    }
    Statement insert(db_, "INSERT INTO jobs VALUES(?1,?2,'PENDING') ON CONFLICT(id) DO NOTHING");
    if (!insert.get() || !insert.bind(1, id.value) || !insert.bind(2, original) ||
        sqlite3_step(insert.get()) != SQLITE_DONE) return db_error(db_);
    Statement read(db_, "SELECT 1 FROM jobs WHERE id=?1 AND original=?2 COLLATE BINARY");
    if (!read.get() || !read.bind(1, id.value) || !read.bind(2, original)) return db_error(db_);
    const int rc = sqlite3_step(read.get());
    if (rc == SQLITE_ROW) return monostate{};
    if (rc == SQLITE_DONE) return Error{ErrorCode::input, "job ID already has a different request"};
    return db_error(db_);
}

Result<JobState> JobStore::state(const JobId& id) {
    Statement read(db_, "SELECT state FROM jobs WHERE id=?1");
    if (!read.get() || !read.bind(1, id.value)) return db_error(db_);
    const int rc = sqlite3_step(read.get());
    if (rc == SQLITE_DONE) return Error{ErrorCode::input, "unknown job"};
    if (rc != SQLITE_ROW) return db_error(db_);
    const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(read.get(), 0));
    if (!value) return db_error(db_);
    for (const auto state : array{JobState::pending, JobState::running, JobState::verified,
            JobState::failed, JobState::cancelled, JobState::blocked, JobState::uncertain}) {
        if (to_string(state) == value) return state;
    }
    return Error{ErrorCode::internal, "invalid stored job state"};
}

JobStore::Status JobStore::transition(const JobId& id, JobState expected, JobState next) {
    if (!can_transition(expected, next)) return Error{ErrorCode::input, "invalid job transition"};
    Statement update(db_, "UPDATE jobs SET state=?1 WHERE id=?2 AND state=?3");
    if (!update.get() || !update.bind(1, to_string(next)) || !update.bind(2, id.value) ||
        !update.bind(3, to_string(expected)) || sqlite3_step(update.get()) != SQLITE_DONE) return db_error(db_);
    if (sqlite3_changes(db_) != 1) return Error{ErrorCode::input, "job missing or state changed"};
    return monostate{};
}

Result<int> JobStore::recover_interrupted() {
    if (!execute(db_, "UPDATE jobs SET state='UNCERTAIN' WHERE state='RUNNING';")) return db_error(db_);
    return sqlite3_changes(db_);
}

}
