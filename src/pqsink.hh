#ifndef PEQUOD_SINK_HH
#define PEQUOD_SINK_HH
#include "pqjoin.hh"
#include "interval.hh"
#include "local_vector.hh"
#include "local_str.hh"
#include "interval_tree.hh"
#include "pqdatum.hh"
#include <tamer/tamer.hh>
#include <list>

namespace pq {
class Server;
class Match;
class RangeMatch;
class JoinRange;
class Interconnect;

class ServerRangeBase {
  public:
    inline ServerRangeBase(Str first, Str last);

    typedef Str endpoint_type;
    inline Str ibegin() const;
    inline Str iend() const;
    inline ::interval<Str> interval() const;
    inline Str subtree_iend() const;
    inline void set_subtree_iend(Str subtree_iend);

    static uint64_t allocated_key_bytes;

  protected:
    LocalStr<24> ibegin_;
    LocalStr<24> iend_;
    Str subtree_iend_;
};

class IntermediateUpdate : public ServerRangeBase {
  public:
    IntermediateUpdate(Str first, Str last, SinkRange* sink, int joinpos, const Match& m, int notifier);

    typedef Str endpoint_type;
    inline Str context() const;
    inline int notifier() const;

    friend std::ostream& operator<<(std::ostream&, const IntermediateUpdate&);

  public:
    rblinks<IntermediateUpdate> rblinks_;
  private:
    LocalStr<12> context_;
    int joinpos_;
    int notifier_;

    friend class SinkRange;
};

class Restart {
  public:
    Restart(SinkRange* sink, int joinpos, const Match& match);
    inline Str context() const;

  private:
    LocalStr<12> context_;
    int joinpos_;

    friend class SinkRange;
};

class SinkRange : public ServerRangeBase {
  public:
    SinkRange(JoinRange* jr, const RangeMatch& rm, uint64_t now);
    ~SinkRange();

    inline void ref();
    inline void deref();

    inline bool valid() const;
    void invalidate();
    inline void prefetch() const;

    inline Join* join() const;
    inline Table* table() const;
    inline Table& make_table_for(Str key) const;
    inline unsigned context_mask() const;
    inline Str context() const;

    inline bool has_expired(uint64_t now) const;

    inline void add_datum(Datum* d) const;
    inline void remove_datum(Datum* d) const;

    void add_update(int joinpos, Str context, Str key, int notifier);
    void add_invalidate(Str key);
    void add_restart(int joinpos, const Match& match);
    inline bool need_update() const;
    inline bool need_restart() const;
    bool update(Str first, Str last, Server& server,
                uint64_t now, tamer::gather_rendezvous& gr);
    bool restart(Str first, Str last, Server& server,
                 uint64_t now, tamer::gather_rendezvous& gr);

    inline void update_hint(const ServerStore& store, ServerStore::iterator hint) const;
    inline Datum* hint() const;

    friend std::ostream& operator<<(std::ostream&, const SinkRange&);

    static uint64_t invalidate_hit_keys;
    static uint64_t invalidate_miss_keys;

  private:
    Table* table_;
    mutable Datum* hint_;
    unsigned context_mask_;
    int dangerous_slot_;
    LocalStr<12> context_;
    uint64_t expires_at_;
    interval_tree<IntermediateUpdate> updates_;
    std::list<Restart*> restarts_;
    int refcount_;
    mutable uintptr_t data_free_;
    mutable local_vector<Datum*, 12> data_;
  protected:
    JoinRange* jr_;
  public:
    rblinks<SinkRange> rblinks_;

    bool update_iu(Str first, Str last, IntermediateUpdate* iu, bool& remaining,
                   Server& server, uint64_t now, tamer::gather_rendezvous& gr);
};

class JoinRange : public ServerRangeBase {
  public:
    JoinRange(Str first, Str last, Join* join);
    ~JoinRange();

    inline Join* join() const;
    inline size_t valid_ranges_size() const;

    bool validate(Str first, Str last, Server& server,
                  uint64_t now, tamer::gather_rendezvous& gr);

  public:
    rblinks<JoinRange> rblinks_;
  private:
    Join* join_;
    interval_tree<SinkRange> valid_ranges_;
    uint64_t flush_at_;

    inline bool validate_one(Str first, Str last, Server& server,
                             uint64_t now, tamer::gather_rendezvous& gr);
    struct validate_args;
    bool validate_step(validate_args& va, int joinpos);
    bool validate_filters(validate_args& va);

    friend class SinkRange;
};

class RemoteRange : public ServerRangeBase {
  public:
    RemoteRange(Str first, Str last, int32_t owner);

    inline int32_t owner() const;
    inline bool pending() const;
    inline void add_waiting(tamer::event<> w);
    inline void notify_waiting();

  public:
    rblinks<RemoteRange> rblinks_;
  private:
    int32_t owner_;
    std::list<tamer::event<>> waiting_;
};

/*
 * A fake sink that represents a remote server. This way the existing
 * source/sink architecture can be used to notify remote servers of
 * changes to data. It has no connection to an actual table or join.
 */
class RemoteSink : public SinkRange {
  public:
    RemoteSink(Interconnect* conn);
    ~RemoteSink();

    inline Interconnect* conn() const;

  private:
    Interconnect* conn_;
};

inline ServerRangeBase::ServerRangeBase(Str first, Str last)
    : ibegin_(first), iend_(last) {
    if (!ibegin_.is_local())
        allocated_key_bytes += ibegin_.length();
    if (!iend_.is_local())
        allocated_key_bytes += iend_.length();
}

inline Str ServerRangeBase::ibegin() const {
    return ibegin_;
}

inline Str ServerRangeBase::iend() const {
    return iend_;
}

inline interval<Str> ServerRangeBase::interval() const {
    return make_interval(ibegin(), iend());
}

inline Str ServerRangeBase::subtree_iend() const {
    return subtree_iend_;
}

inline void ServerRangeBase::set_subtree_iend(Str subtree_iend) {
    subtree_iend_ = subtree_iend;
}

inline Join* JoinRange::join() const {
    return join_;
}

inline size_t JoinRange::valid_ranges_size() const {
    return valid_ranges_.size();
}

inline void SinkRange::ref() {
    ++refcount_;
}

inline void SinkRange::deref() {
    if (--refcount_ == 0 && !valid())
        delete this;
}

inline bool SinkRange::valid() const {
    return table_;
}

inline Join* SinkRange::join() const {
    return jr_->join();
}

inline Table* SinkRange::table() const {
    return table_;
}

inline unsigned SinkRange::context_mask() const {
    return context_mask_;
}

inline Str SinkRange::context() const {
    return context_;
}

inline bool SinkRange::has_expired(uint64_t now) const {
    return expires_at_ && expires_at_ < now;
}

inline void SinkRange::add_datum(Datum* d) const {
    assert(d->owner() == this);
    uintptr_t pos = data_free_;
    if (pos == uintptr_t(-1)) {
        pos = data_.size();
        data_.push_back(d);
    } else {
        data_free_ = (uintptr_t) data_[pos];
        data_[pos] = d;
    }
    d->owner_position_ = pos;
}

inline void SinkRange::remove_datum(Datum* d) const {
    assert(d->owner() == this
           && (size_t) d->owner_position_ < (size_t) data_.size());
    data_[d->owner_position_] = (Datum*) data_free_;
    data_free_ = d->owner_position_;
}

inline bool SinkRange::need_update() const {
    return !updates_.empty();
}

inline bool SinkRange::need_restart() const {
    return !restarts_.empty();
}

inline void SinkRange::update_hint(const ServerStore& store, ServerStore::iterator hint) const {
#if HAVE_HINT_ENABLED
    Datum* hd = hint == store.end() ? 0 : hint.operator->();
    if (hd)
        hd->ref();
    if (hint_)
        hint_->deref();
    hint_ = hd;
#else
    (void)store;
    (void)hint;
#endif
}

inline Datum* SinkRange::hint() const {
    return hint_ && hint_->valid() ? hint_ : 0;
}

inline Str IntermediateUpdate::context() const {
    return context_;
}

inline int IntermediateUpdate::notifier() const {
    return notifier_;
}

inline Str Restart::context() const {
    return context_;
}

inline int32_t RemoteRange::owner() const {
    return owner_;
}

inline bool RemoteRange::pending() const {
    return !waiting_.empty();
}

inline void RemoteRange::add_waiting(tamer::event<> w) {
    waiting_.push_back(w);
}

inline void RemoteRange::notify_waiting() {
    while(!waiting_.empty()) {
        waiting_.front().operator()();
        waiting_.pop_front();
    }
}

inline Interconnect* RemoteSink::conn() const {
    return conn_;
}

} // namespace pq
#endif
