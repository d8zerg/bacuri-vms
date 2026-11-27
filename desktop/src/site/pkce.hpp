#pragma once

#include <QByteArray>
#include <QString>
#include <QUrlQuery>

#include <optional>

namespace bacuri::vms::client
{
// PKCE parameters of one sign-in: a random verifier and its S256 challenge, and a random state
// that ties the answer on the loopback address to this sign-in.
struct Pkce {
    QString verifier;
    QString challenge;
    QString state;
};

QString base64url(const QByteArray& bytes);
QString s256(const QString& verifier);
Pkce new_pkce();

// The answer of the browser on the loopback address.
struct Callback {
    QString code;
    QString error;
};

// Reads the request line of an HTTP request on the loopback address, such as
// "GET /callback?code=...&state=... HTTP/1.1". Returns nothing for another path, a missing or
// different state, or neither a code nor an error.
std::optional<Callback> read_callback(const QByteArray& request_line, const QString& path, const QString& state);
} // namespace bacuri::vms::client
