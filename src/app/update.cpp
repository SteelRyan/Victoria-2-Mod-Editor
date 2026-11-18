// update.cpp — see update.h
#include "app/update.h"
#include "app/version.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <mutex>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

namespace update {

static const char *K_ON = "update/check", *K_LAST = "update/last", *K_LATEST = "update/latest",
                  *K_HIDDEN = "update/hidden";
static constexpr qint64 DAY = 24 * 3600;
static Checker *g_instance = nullptr;

std::optional<std::array<int, 3>> parse_version(const QString &tag)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*[vV]?(\d{1,4})(?:\.(\d{1,4}))?(?:\.(\d{1,4}))?\s*$)"));
    const auto m = re.match(tag);
    if (!m.hasMatch())
        return std::nullopt;
    std::array<int, 3> v{0, 0, 0};
    for (int i = 0; i < 3; ++i)
        v[size_t(i)] = m.captured(i + 1).isEmpty() ? 0 : m.captured(i + 1).toInt();
    return v;
}

QString text_of(const std::array<int, 3> &v)
{
    return QStringLiteral("%1.%2.%3").arg(v[0]).arg(v[1]).arg(v[2]);
}

std::optional<QString> newer_than(const QString &tag, const QString &mine)
{
    const auto a = parse_version(tag), b = parse_version(mine);
    if (!a || !b || !(*a > *b))
        return std::nullopt;
    return text_of(*a);
}

// ------------------------------------------------------------------------------------------------- asking GitHub
namespace {
struct Handle {                                        // a WinHTTP handle that closes itself
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET x) : h(x) {}
    ~Handle()
    {
        if (h)
            WinHttpCloseHandle(h);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    explicit operator bool() const { return h != nullptr; }
};
}  // namespace

std::optional<QString> fetch_latest_tag()
{
    const std::wstring agent = L"ModEditor/" + QStringLiteral(MODEDITOR_VERSION_TEXT).toStdWString();
    Handle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
        return std::nullopt;
    WinHttpSetTimeouts(session.h, 10000, 10000, 10000, 10000);
    Handle conn(WinHttpConnect(session.h, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!conn)
        return std::nullopt;
    Handle req(WinHttpOpenRequest(conn.h, L"GET", L"/repos/SteelRyan/Victoria-2-Mod-Editor/releases/latest", nullptr,
                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!req)
        return std::nullopt;
    DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;   // the answer comes from api.github.com or not at all
    WinHttpSetOption(req.h, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never));
    // (certificate errors are never ignored: WinHTTP refuses the connection and this returns nothing)
    const wchar_t *headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpSendRequest(req.h, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.h, nullptr))
        return std::nullopt;
    DWORD status = 0, len = sizeof(status);
    if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &len, WINHTTP_NO_HEADER_INDEX) ||
        status != 200)
        return std::nullopt;
    QByteArray body;
    constexpr qsizetype CAP = 256 * 1024;
    while (true) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail))
            return std::nullopt;
        if (avail == 0)
            break;
        if (body.size() + qsizetype(avail) > CAP)
            return std::nullopt;
        const qsizetype at = body.size();
        body.resize(at + qsizetype(avail));
        DWORD got = 0;
        if (!WinHttpReadData(req.h, body.data() + at, avail, &got))
            return std::nullopt;
        body.resize(at + qsizetype(got));
    }
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return std::nullopt;
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("draft")).toBool() || o.value(QStringLiteral("prerelease")).toBool())
        return std::nullopt;
    const QString tag = o.value(QStringLiteral("tag_name")).toString();
    if (!parse_version(tag))
        return std::nullopt;
    return tag;
}

// ------------------------------------------------------------------------------------------------- the checker
struct Shared {
    std::mutex m;
    bool done = false;
    std::optional<QString> tag;
};

Checker::Checker(QSettings &settings_, QObject *parent) : QObject(parent), settings(settings_)
{
    g_instance = this;
    _poll.setInterval(400);
    connect(&_poll, &QTimer::timeout, this, &Checker::_collect);
}

Checker::~Checker()
{
    if (g_instance == this)
        g_instance = nullptr;
}

Checker *Checker::instance() { return g_instance; }

bool Checker::enabled() const { return settings.value(QString::fromLatin1(K_ON), true).toBool(); }

void Checker::set_enabled(bool on)
{
    settings.setValue(QString::fromLatin1(K_ON), on);
    if (on)
        start();
    Q_EMIT changed();
}

std::optional<QString> Checker::newer() const
{
    if (!enabled())
        return std::nullopt;
    const auto n = newer_than(settings.value(QString::fromLatin1(K_LATEST)).toString(),
                              QStringLiteral(MODEDITOR_VERSION_TEXT));
    if (!n || settings.value(QString::fromLatin1(K_HIDDEN)).toString() == *n)
        return std::nullopt;
    return n;
}

void Checker::hide_this_one()
{
    if (const auto n = newer())
        settings.setValue(QString::fromLatin1(K_HIDDEN), *n);
    Q_EMIT changed();
}

void Checker::start()
{
    if (!enabled() || _job)
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 last = settings.value(QString::fromLatin1(K_LAST), 0).toLongLong();
    if (last > 0 && now - last < DAY && now >= last)
        return;
    _job = std::make_shared<Shared>();
    // a thread of its own, left to run: it holds only the shared answer box, so a slow network never keeps the
    // window waiting or the program from closing
    std::thread([job = _job] {
        auto tag = fetch_latest_tag();
        std::lock_guard<std::mutex> lock(job->m);
        job->tag = std::move(tag);
        job->done = true;
    }).detach();
    _poll.start();
}

void Checker::_collect()
{
    std::optional<QString> tag;
    {
        std::lock_guard<std::mutex> lock(_job->m);
        if (!_job->done)
            return;
        tag = _job->tag;
    }
    _poll.stop();
    _job.reset();
    if (!tag)                                           // no answer (offline, GitHub down): ask again next start
        return;
    settings.setValue(QString::fromLatin1(K_LAST), QDateTime::currentSecsSinceEpoch());
    settings.setValue(QString::fromLatin1(K_LATEST), *tag);
    Q_EMIT changed();
}

}  // namespace update
