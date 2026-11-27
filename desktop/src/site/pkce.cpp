#include "site/pkce.hpp"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QUrl>

#include <array>

namespace bacuri::vms::client
{
namespace
{
QByteArray random_bytes(qsizetype count)
{
    QByteArray bytes(count, Qt::Uninitialized);
    auto* generator = QRandomGenerator::system();
    for (auto& byte : bytes)
        byte = static_cast<char>(generator->bounded(256));
    return bytes;
}
} // namespace

QString base64url(const QByteArray& bytes)
{
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString s256(const QString& verifier)
{
    return base64url(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256));
}

Pkce new_pkce()
{
    Pkce pkce;
    pkce.verifier = base64url(random_bytes(48));
    pkce.challenge = s256(pkce.verifier);
    pkce.state = base64url(random_bytes(24));
    return pkce;
}

std::optional<Callback> read_callback(const QByteArray& request_line, const QString& path, const QString& state)
{
    const auto parts = request_line.trimmed().split(' ');
    if (parts.size() != 3 || parts[0] != "GET" || !parts[2].startsWith("HTTP/1."))
        return std::nullopt;
    const QUrl target{QString::fromLatin1(parts[1])};
    if (!target.isValid() || target.path() != path)
        return std::nullopt;
    const QUrlQuery query{target};
    if (query.queryItemValue("state") != state || state.isEmpty())
        return std::nullopt;
    Callback callback{query.queryItemValue("code"), query.queryItemValue("error")};
    if (callback.code.isEmpty() == callback.error.isEmpty())
        return std::nullopt;
    return callback;
}
} // namespace bacuri::vms::client
