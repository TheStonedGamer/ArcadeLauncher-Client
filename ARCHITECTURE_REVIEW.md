# ArcadeLauncher — Architecture & Product Review

_Prepared in response to `social.md`. A professional audit of the platform as it
exists today, plus a prioritized plan to bring the social layer, notifications,
downloads, and infrastructure to Steam/Discord/Battle.net quality. Grounded in
the real codebase — not a greenfield wishlist._

**Scope of what exists today (the honest baseline):**
- **Client** — C++17/Win32/Direct2D launcher; one WinHTTP WebSocket to
  `/ws/social`; WASAPI **raw-PCM** voice relayed as binary frames; social
  personalization (favorites/nicknames/notif toggles) stored **client-local** in
  `social_prefs.json`; notifications are an **in-memory, session-only** capped
  history + toast queue.
- **Server** — Rust/axum 0.7/tokio, MariaDB via `mysql_async`, single binary
  (`include!`-split). `social_api.rs` holds friends/requests/block REST, DM
  history, and the gateway. Presence/routing live in an **in-process
  `social_hub` map** (uid → sender). Argon2 + TOTP + bearer tokens. No Redis, no
  message queue, no object storage, no metrics/observability stack.
- **Companion** — ArcadeLauncher-Requests (separate binary, shared DB).

The three structural truths that shape every recommendation below:
1. **`social_hub` is in-process** → the server cannot run more than one instance
   without losing presence and cross-user routing. This is the #1 scaling
   ceiling.
2. **Notifications and social prefs do not persist server-side** → nothing syncs
   across devices and everything resets on restart. This is the #1 UX gap.
3. **Voice is server-relayed raw PCM** → no codec, no jitter buffer, no NAT
   traversal; server bandwidth scales O(participants²) per call.

---

## 1. Features needing refinement

| # | Feature | Why it needs work | How Steam/Discord do it | Proposed direction | Difficulty | Priority |
|---|---------|-------------------|-------------------------|--------------------|-----------|----------|
| 1.1 | **Notifications** | In-memory only; lost on restart, no cross-device sync, no read state, no badge persistence | Server-stored notification rows with read/seen state, synced on connect | Persist to MariaDB; deliver unseen on gateway connect; ack read-state back (see §6) | M | **P0** |
| 1.2 | **Presence / `social_hub`** | Single-process in-memory map; no horizontal scale; presence wrong if process restarts | Redis-backed presence with pub/sub fan-out across gateway nodes | Move presence + routing to Redis (`PUBLISH`/`SUBSCRIBE` + `SETEX` TTL keys) | M–L | **P0** |
| 1.3 | **Voice** | Raw PCM, server-relayed, no codec/jitter/NAT | Opus + jitter buffer; SFU or P2P with TURN fallback | Opus encode/decode + jitter buffer client-side; Coturn for NAT; keep relay as fallback | L | **P1** |
| 1.4 | **Friend requests** | Race conditions, no expiry, no rate limit, cancel/ignore conflated | Explicit state machine, idempotent transitions, rate limits | Full redesign (see §5) | M | **P0** |
| 1.5 | **DMs** | No reactions/edits/replies/attachments/read receipts; history not paginated; no offline queue | Discord-style message objects with edit history, reactions, pagination | Extend message schema + WS events (see §4) | M–L | **P1** |
| 1.6 | **Downloads** | Single connection per file; no parallel chunks, delta patch, verify/repair, LAN cache | Steam CDN + chunked content + delta patching + LAN peer cache | Multi-connection chunking + a verify/repair pass first; CDN/peer later (see §6 of prompt → §9 here) | M–L | **P2** |
| 1.7 | **Social prefs** | Client-local only; lost on reinstall, no multi-device | Server-synced account preferences | Add `user_prefs` table + sync endpoint (see §7) | S–M | **P1** |
| 1.8 | **Reconnect/offline** | Reconnect re-pulls friends + open convos, but missed notifications/messages aren't backfilled durably | Sequence-numbered event log, client sends last-seen cursor | Per-user monotonic `event_seq`; client resumes from cursor | M | **P1** |
| 1.9 | **Rate limiting / abuse** | None visible on REST or gateway | Per-token, per-route token buckets | `tower-governor` middleware + per-action limits | S–M | **P1** |
| 1.10 | **Observability** | No metrics/tracing/structured logs/audit | Prometheus + structured logs + audit trail | `tracing` + `metrics`/Prometheus exporter + `audit_log` table | M | **P2** |

---

## 2. Missing features (by area)

**Friends:** friend groups/custom categories, pinning, mutual-friends, suggested
friends, recently-played-together, friend activity feed, rich profiles, friend
notes (server-synced), friend search across a large list.

**Presence:** custom status text, invisible mode, do-not-disturb, idle
auto-detection (no input → away), per-session device indicator (desktop/mobile),
"join game", "spectate", party presence, rich activity payloads.

**DMs:** reactions, replies/threads, edits, deletion (soft), attachments &
screenshots, GIF/emoji picker, read receipts, unread badges (persisted), search,
pins, infinite history paging, message caching (SQLite client-side), offline send
queue.

**Notifications:** persistent notification center, per-user & per-category mute,
sound customization, quiet hours, grouping, badge counts, cross-device sync,
offline delivery.

**Voice/Comms:** voice rooms, group calls, push-to-talk, voice-activation gate,
device selection, echo cancel / noise suppression, Opus, jitter buffer, packet
loss concealment, STUN/TURN, screen-share & video scaffolding.

**Communities:** group chats, communities/servers with channels, party chat,
temporary game lobbies.

**Library:** playtime tracking, last-played, completion status, user
ratings/tags/notes, achievements, DLC, series grouping, multiplayer tags, user
screenshots, smart/dynamic collections, duplicate detection, per-game launch
profiles & args, per-game controller profiles, multi-disc management, on-screen
keyboard for Big Picture.

**Cloud:** Saves v2 (conflict resolution, multiple slots, version history,
encryption, selective sync), cloud config (settings/controller mappings/metadata
edits sync).

**Platform:** plugin system, theme engine, localization, telemetry, crash
reporting, diagnostics export, backup/restore, portable mode, DB migrations,
admin analytics, full-text search, file dedup, API versioning, feature flags,
audit logs.

---

## 3. New systems to add (infrastructure)

| System | Purpose | Tech | When |
|--------|---------|------|------|
| **Redis** | Presence store, pub/sub fan-out across gateway nodes, rate-limit buckets, ephemeral caches | Redis 7 (or KeyDB) | P0 — unlocks horizontal scale |
| **Event sequencing** | Per-user monotonic `event_seq` so clients resume from a cursor | MariaDB column + Redis counter | P0/P1 |
| **Object storage** | DM attachments, user screenshots, avatars/banners | MinIO (S3-compatible), self-hosted | P1 |
| **Coturn (STUN/TURN)** | NAT traversal for voice/video | coturn in Docker | P1 |
| **Media pipeline** | Thumbnail/transcode attachments & screenshots | FFmpeg (server-side worker) | P2 |
| **Background jobs** | Hashing, IGDB enrich, email, media transcode, save-pack compression | A job runner (Redis-backed `apalis`, or a dedicated worker binary) | P1 |
| **Search index** | Full-text search of catalog + messages | MariaDB FULLTEXT first; Meilisearch if it grows | P2 |
| **Monitoring stack** | Metrics/alerting/dashboards | Prometheus + Grafana; Loki for logs | P2 |
| **DB migrations** | Versioned, reproducible schema | `sqlx::migrate!` or `refinery` | P1 (do early; cheap now, painful later) |
| **Gateway service (optional)** | Split `/ws/social` into its own horizontally-scaled binary | Separate axum bin, Redis-coordinated | P3 (only at real scale) |

---

## 4. Social redesign (target: Steam/Discord/Battle.net parity)

**Design principle:** model the social layer as if it could ship as a standalone
product. Everything user-visible is server-authoritative and synced; the client
caches in SQLite and reconciles from a sequence cursor.

### 4.1 Message object (Discord-shaped)
```sql
CREATE TABLE dm_messages (
  id            BIGINT PRIMARY KEY AUTO_INCREMENT,
  conversation_id BIGINT NOT NULL,
  sender_id     BIGINT NOT NULL,
  reply_to_id   BIGINT NULL,
  body          TEXT NOT NULL,
  attachments   JSON NULL,          -- [{url,kind,w,h,size}]
  edited_at     DATETIME NULL,
  deleted_at    DATETIME NULL,      -- soft delete
  created_at    DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  event_seq     BIGINT NOT NULL,    -- per-conversation monotonic
  INDEX (conversation_id, id),
  INDEX (conversation_id, event_seq)
);
CREATE TABLE dm_reactions (
  message_id BIGINT, user_id BIGINT, emoji VARCHAR(64),
  PRIMARY KEY (message_id, user_id, emoji)
);
CREATE TABLE dm_read_state (
  conversation_id BIGINT, user_id BIGINT,
  last_read_message_id BIGINT, updated_at DATETIME,
  PRIMARY KEY (conversation_id, user_id)
);
```

### 4.2 WebSocket events (additions)
Client→server: `message.send`, `message.edit`, `message.delete`,
`reaction.add`, `reaction.remove`, `typing.start`, `read.ack`,
`presence.update` (status/custom/DND/invisible), `resume {last_seq}`.
Server→client: `message.created/edited/deleted`, `reaction.updated`,
`typing`, `read.receipt`, `presence`, `notification`, `friend.*`, `resume.batch`.

All events carry `seq`; client persists `max(seq)` and sends it on `resume` to
backfill exactly what it missed (replaces the current "re-pull everything").

### 4.3 Presence model
Statuses: `online / away(idle) / dnd / invisible / offline`, plus a `custom_text`
and a structured `activity` (`{game_id, state, party{size,max,join_token}}`).
Idle is client-detected (no input N minutes → away). Invisible suppresses
presence broadcast but keeps the socket. Store live presence in Redis
(`HSET presence:<uid>` + `EXPIRE`), broadcast diffs via pub/sub.

### 4.4 Communities (phase 2)
`communities` → `channels` (text/voice) → `channel_messages`, with
`community_members` + role flags. Reuse the DM message machinery; a channel is
just a conversation with many members and a permission row.

---

## 5. Friend-request redesign

### 5.1 State machine
```
none ──request──▶ pending_out / pending_in
pending_in ──accept──▶ friends
pending_in ──decline──▶ none      (silent to requester)
pending_in ──ignore──▶ ignored    (auto-declines future, no notify)
pending_out ──cancel──▶ none
any ──block──▶ blocked            (auto-decline + hide)
blocked ──unblock──▶ none
friends ──remove──▶ none
```

### 5.2 Schema
```sql
CREATE TABLE relationships (
  user_id     BIGINT NOT NULL,
  other_id    BIGINT NOT NULL,
  state       ENUM('pending_out','pending_in','friends','blocked','ignored') NOT NULL,
  created_at  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  expires_at  DATETIME NULL,          -- request expiry
  PRIMARY KEY (user_id, other_id),
  INDEX (other_id, state)
);
```
Store **both directions** as rows in one transaction so each side queries only
`WHERE user_id = me`. The pair `(A,B)` and `(B,A)` are written/updated atomically.

### 5.3 Edge cases & abuse
- **Simultaneous requests both ways** → detect in the transaction; if both sides
  are `pending`, collapse to `friends` immediately (idempotent).
- **Duplicate request** → no-op (upsert), never a second notification.
- **Expiry** → `expires_at` (e.g. 30 days); a janitor job sweeps stale `pending_*`.
- **Rate limiting** → token bucket per requester (e.g. 20 requests/hour, 3/target/day).
- **Privacy** → `who_can_friend_me` (everyone / friends-of-friends / no-one) and
  `who_can_dm_me` (everyone / friends-only).
- **Blocked** → requests auto-decline; sender sees no signal; DMs rejected.
- **Username changes** → relationships key on stable `user_id`, never username.

### 5.4 REST + WS
REST: `POST /api/social/requests {target}`, `POST /requests/{id}/{accept|decline|ignore|cancel}`,
`POST /api/social/block/{uid}`, `DELETE /api/social/friends/{uid}`.
WS: `friend.request`, `friend.accepted`, `friend.removed`, `friend.blocked` —
all sequenced and reconciled on reconnect.

---

## 6. Notification redesign

### 6.1 Schema
```sql
CREATE TABLE notifications (
  id         BIGINT PRIMARY KEY AUTO_INCREMENT,
  user_id    BIGINT NOT NULL,
  category   VARCHAR(32) NOT NULL,   -- friend|message|voice|system|catalog
  priority   TINYINT NOT NULL DEFAULT 1,
  title      VARCHAR(256), body TEXT,
  payload    JSON,                   -- deep-link data
  group_key  VARCHAR(128) NULL,      -- collapse "3 new messages from X"
  seen_at    DATETIME NULL,
  read_at    DATETIME NULL,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  event_seq  BIGINT NOT NULL,
  INDEX (user_id, event_seq), INDEX (user_id, read_at)
);
CREATE TABLE notification_settings (
  user_id BIGINT, category VARCHAR(32),
  muted BOOL DEFAULT 0, sound VARCHAR(64) NULL,
  PRIMARY KEY (user_id, category)
);
```

### 6.2 Delivery architecture
- On gateway connect, client sends `resume {last_seq}`; server streams all
  notifications with `event_seq > last_seq` (offline delivery, exactly-once-ish).
- Live notifications publish through **Redis pub/sub** keyed by `uid` so any
  gateway node delivers to the user's socket(s) wherever they're connected.
- **Badge counts** = `COUNT(read_at IS NULL)`, cached in Redis, decremented on
  `read.ack`.
- **Grouping** via `group_key`; **quiet hours** + per-category mute/sound applied
  server-side before push (and mirrored client-side for instant toasts).
- Toasts remain the existing client queue, now backed by durable rows.

### 6.3 Event bus
Introduce a small internal event bus (`tokio::sync::broadcast` per node + Redis
pub/sub across nodes). Producers (friend accept, DM, catalog add) emit a
`DomainEvent`; a notification service consumes, persists, and fans out. This
decouples "something happened" from "notify these users."

---

## 7. Architecture recommendations (consolidated)

1. **Adopt Redis now (P0).** Presence (`presence:<uid>` hashes + TTL), pub/sub
   routing for gateway fan-out, rate-limit buckets, badge counts. This single
   change removes the "one server process forever" ceiling.
2. **Introduce DB migrations now (P1, cheap today).** `sqlx::migrate!` or
   `refinery`. Every schema change below should land as a numbered migration.
3. **Sequence everything (P0/P1).** Per-user (and per-conversation) monotonic
   `event_seq`. Reconnect resumes from a cursor instead of re-pulling — correct,
   cheap, and the foundation for offline delivery.
4. **Server-authoritative preferences (P1).** Move `social_prefs.json` content
   into a `user_prefs` table; the client keeps a cache but the server is source
   of truth → multi-device.
5. **Object storage for media (P1).** MinIO for attachments/screenshots/avatars;
   never stream user uploads through the app process memory.
6. **Voice: codec + NAT (P1).** Opus + jitter buffer client-side; Coturn for
   STUN/TURN; keep the server relay as a last-resort fallback. An SFU
   (e.g. mediasoup/LiveKit) only if group calls/screen-share become real.
7. **Background job runner (P1).** Move hashing, IGDB enrich, email, media
   transcode, and save-pack compression off request paths onto a Redis-backed
   worker (`apalis`) or a dedicated worker binary.
8. **Observability (P2).** `tracing` structured logs → Loki; `metrics` →
   Prometheus → Grafana; `audit_log` table for admin/security actions.
9. **Rate limiting + API versioning (P1/P2).** `tower-governor` on sensitive
   routes; prefix the API `/api/v1/` and gate breaking changes behind it
   (reinforces the existing major.minor lockstep).
10. **Gateway extraction (P3, only at real scale).** Split `/ws/social` into its
    own horizontally-scaled binary once Redis pub/sub is in place; the monolith
    stays for REST/catalog/admin.

**Target service topology (Docker/Proxmox):**
```
            ┌────────────┐
  clients ──┤   nginx    ├── TLS, /ws/ scoped upgrade
            └─────┬──────┘
        ┌─────────┼───────────┬────────────┐
   ┌────▼───┐ ┌───▼────┐ ┌────▼─────┐ ┌────▼─────┐
   │  API   │ │ gateway│ │ requests │ │  worker  │
   │ (axum) │ │ (ws)   │ │  (8723)  │ │  (jobs)  │
   └───┬────┘ └───┬────┘ └────┬─────┘ └────┬─────┘
       └──────────┴────┬──────┴────────────┘
              ┌────────▼────────┐   ┌─────────┐  ┌────────┐
              │     MariaDB     │   │  Redis  │  │  MinIO │
              └─────────────────┘   └─────────┘  └────────┘
                                    ┌─────────┐
                                    │ coturn  │  (voice NAT)
                                    └─────────┘
```

---

## 8. Database changes (summary)
New/changed tables: `relationships` (replaces ad-hoc friend/block rows),
`dm_messages` + `dm_reactions` + `dm_read_state`, `notifications` +
`notification_settings`, `user_prefs`, `user_profiles` (avatar/banner/bio/level),
`playtime` + `game_user_state` (rating/tags/notes/completion), `audit_log`,
`schema_migrations`. Add `event_seq` counters (per-user, per-conversation). Add
FULLTEXT indexes on catalog title/desc and (optionally) message bodies.

## 9. API additions (summary)
`/api/v1/...` prefix. Social: requests state-machine routes (§5.4), DM
edit/delete/react/read, message history pagination (`?before=<id>&limit=`),
attachment upload (presigned MinIO PUT). Profiles: `GET/PUT /api/v1/profile`,
avatars/banners. Prefs: `GET/PUT /api/v1/prefs`. Library: playtime ingest,
ratings/tags/notes, cloud-saves v2 (slots, versions, conflict).

## 10. WebSocket event additions (summary)
`resume{last_seq}`/`resume.batch`, `message.*`, `reaction.*`, `typing`,
`read.ack`/`read.receipt`, `presence.update`/`presence`, `notification`,
`friend.*`, `voice.{join,leave,signal}` (now carrying SDP/ICE for TURN),
`party.*`, `activity`. Every server→client event carries a `seq`.

---

## 11. Prioritized implementation roadmap

**Phase 0 — Foundations (unblock scale & correctness)**
- Add Redis; move presence + gateway routing onto it.
- Introduce DB migrations.
- Add per-user `event_seq` + `resume{last_seq}` reconnect.
- Persist notifications to MariaDB; deliver unseen on connect.

**Phase 1 — Social parity**
- Friend-request state machine (§5) + rate limiting + privacy controls.
- DM upgrades: read receipts, reactions, edit/delete, pagination, offline queue,
  client-side SQLite cache.
- Server-synced preferences + user profiles (avatar/bio).
- Object storage (MinIO) for attachments/screenshots/avatars.

**Phase 2 — Comms & library depth**
- Voice: Opus + jitter buffer + Coturn; push-to-talk/voice-activation, device
  selection, noise suppression.
- Library: playtime, last-played, ratings/tags/notes, smart collections,
  per-game launch profiles.
- Cloud Saves v2 (conflict resolution, slots, versioning, encryption).

**Phase 3 — Platform & scale**
- Group chats / communities / channels; party + game lobbies.
- Observability stack, audit logs, feature flags, admin analytics.
- Download v2 (parallel chunks, verify/repair, delta patch, LAN/peer cache).
- Gateway service extraction; search index (Meilisearch) if needed.

---

## 12. Suggested folder structure (server, still one repo)
```
src/
  api/        catalog, manifest, downloads, account, profiles, prefs
  social/     relationships, dm, presence, notifications, events
  gateway/    ws upgrade, session pump, resume/seq, redis fanout
  voice/      signaling, turn creds
  infra/      redis, db pool, migrations, object store, job queue
  jobs/       hashing, igdb, email, media transcode, save compression
  admin/      html ui, analytics, audit
migrations/   0001_init.sql, 0002_relationships.sql, ...
```
(Implement as modules; only split into separate binaries — `gateway`, `worker` —
when load demands it.)

## 13. Suggested service architecture
See topology diagram in §7. Start as **one binary + Redis + MariaDB + MinIO +
Coturn**, all in Docker on the existing Proxmox host. Extract `gateway` and
`worker` into their own binaries only when a single process is the bottleneck —
Redis pub/sub makes that split a config change, not a rewrite.

## 14. Suggested third-party libraries
**Server (Rust):** `redis`/`fred`, `sqlx` (migrations + compile-checked queries)
or keep `mysql_async` + `refinery`, `apalis` (jobs), `tower-governor` (rate
limit), `tracing` + `tracing-subscriber` + `metrics-exporter-prometheus`,
`aws-sdk-s3`/`rust-s3` (MinIO), `webrtc-rs` or `str0m` (if SFU), `lettre` (email,
likely already), `opus`/`audiopus` if any server-side audio work.
**Client (C++):** `libopus` (voice codec), `libwebrtc` or hand-rolled jitter
buffer + `webrtc-audio-processing` (AEC/NS), `SQLite` (local message/notification
cache), `libdatachannel` (STUN/TURN/ICE) if moving voice P2P.
**Infra:** Redis/KeyDB, MariaDB, MinIO, coturn, Prometheus + Grafana + Loki,
Meilisearch (optional), FFmpeg.

## 15. Suggested future roadmap (Steam-feature triage)
**Recommended:** user profiles (avatar/banner/bio/level), playtime + last-played,
activity feed, user screenshots, achievements (where emulator cores expose them),
friend groups, communities/channels, library/family sharing model, news/events
on game pages.
**Nice to have:** broadcasting, remote-play scaffolding, recommendations, game
comments/reviews, badges/seasonal events, on-screen keyboard for Big Picture.
**Not worth implementing (for a private, self-hosted platform):** trading cards,
inventory/marketplace, Workshop-scale mod hosting, monetized cosmetics. These
carry large surface area with little payoff at this audience size — skip or stub.

---

### Bottom line
The platform is in genuinely good shape for its size, and the social layer
already _works_. The gap to Steam/Discord is not features-on-screen — it's three
infrastructure investments that everything else hangs off of: **Redis-backed
presence/fan-out**, **server-persisted + sequenced state** (notifications,
messages, prefs), and **a real voice stack** (Opus + jitter buffer + TURN). Do
Phase 0 first; it converts the social system from "single-process demo quality"
to "scales to thousands of remote users," and every later feature becomes
straightforward once state is durable, sequenced, and fan-out-able.
