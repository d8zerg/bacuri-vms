BEGIN;
-- A desktop client holds its session in an Authorization header instead of a browser cookie.
-- The client kind keeps a cookie from being accepted as a desktop token and the reverse.
ALTER TABLE bacuri_access.browser_sessions
    ADD COLUMN client text NOT NULL DEFAULT 'browser' CHECK (client IN ('browser', 'desktop'));
-- One-time codes that a signed-in browser issues for a desktop client on the same computer.
-- The client proves possession of the PKCE verifier and the loopback address when it exchanges a code.
CREATE TABLE bacuri_access.desktop_codes (
    code_hash text PRIMARY KEY,
    browser_hash text NOT NULL REFERENCES bacuri_access.browser_sessions(token_hash) ON DELETE CASCADE,
    challenge text NOT NULL CHECK (challenge ~ '^[A-Za-z0-9_-]{43}$'),
    redirect_uri text NOT NULL,
    expires_at timestamptz NOT NULL
);
CREATE INDEX desktop_codes_expiry ON bacuri_access.desktop_codes(expires_at);
INSERT INTO bacuri_meta.schema_migrations VALUES (32);
COMMIT;
