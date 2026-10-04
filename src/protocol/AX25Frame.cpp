/*
 * AX25Frame.cpp - AX.25 v2.2 frames held in decoded form.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "AX25Frame.h"

#include <QRegularExpression>

#include "DirewolfHeaders.h"

namespace ax25 {

quint16 fcsCalc(const QByteArray &data)
{
    return fcs_calc(reinterpret_cast<unsigned char *>(const_cast<char *>(data.constData())),
                    data.size());
}

QByteArray fcsAppend(const QByteArray &data)
{
    const quint16 fcs = fcsCalc(data);
    QByteArray out = data;
    out.append(static_cast<char>(fcs & 0xFF));         // least significant octet first
    out.append(static_cast<char>((fcs >> 8) & 0xFF));
    return out;
}

bool fcsCheck(const QByteArray &dataWithFcs)
{
    if (dataWithFcs.size() < 3) return false;
    const QByteArray data = dataWithFcs.left(dataWithFcs.size() - 2);
    const quint16 fcs = fcsCalc(data);
    return static_cast<quint8>(dataWithFcs.at(dataWithFcs.size() - 2)) == (fcs & 0xFF)
        && static_cast<quint8>(dataWithFcs.at(dataWithFcs.size() - 1)) == ((fcs >> 8) & 0xFF);
}

bool splitCallsign(const QString &textIn, QString &call, int &ssid, QString *error)
{
    static const QRegularExpression callRe(QStringLiteral("^[A-Z0-9]{1,6}$"));

    const QString text = textIn.trimmed().toUpper();
    if (text.isEmpty()) {
        if (error) *error = QStringLiteral("Empty callsign");
        return false;
    }

    const int dash = text.indexOf('-');
    if (dash >= 0) {
        call = text.left(dash);
        bool ok = false;
        ssid = text.mid(dash + 1).toInt(&ok);
        if (!ok) {
            if (error) *error = QStringLiteral("Invalid SSID in '%1'").arg(text);
            return false;
        }
    } else {
        call = text;
        ssid = 0;
    }

    if (!callRe.match(call).hasMatch()) {
        if (error) *error = QStringLiteral("Invalid callsign '%1': 1 to 6 characters, A-Z and 0-9 only").arg(call);
        return false;
    }
    if (ssid < 0 || ssid > 15) {
        if (error) *error = QStringLiteral("Invalid SSID %1: must be 0 to 15").arg(ssid);
        return false;
    }
    return true;
}

QString formatCallsign(const QString &call, int ssid)
{
    return ssid ? QStringLiteral("%1-%2").arg(call).arg(ssid) : call;
}

QByteArray encodeText(const QString &text, const QString &encoding)
{
    if (encoding.compare(QStringLiteral("ascii"), Qt::CaseInsensitive) == 0) {
        QByteArray out;
        out.reserve(text.size());
        for (const QChar c : text) {
            out.append(c.unicode() < 0x80 ? static_cast<char>(c.unicode()) : '?');
        }
        return out;
    }
    return text.toUtf8();
}

QString decodeText(const QByteArray &bytes, const QString &encoding)
{
    if (encoding.compare(QStringLiteral("ascii"), Qt::CaseInsensitive) == 0) {
        QString out;
        out.reserve(bytes.size());
        for (const char c : bytes) {
            const auto u = static_cast<unsigned char>(c);
            out.append(u < 0x80 ? QChar(u) : QChar(0xFFFD));
        }
        return out;
    }
    return QString::fromUtf8(bytes);   // invalid sequences become U+FFFD
}

} // namespace ax25


// ---- AX25Address --------------------------------------------------------------

std::optional<AX25Address> AX25Address::parse(const QString &textIn, QString *error)
{
    QString text = textIn.trimmed().toUpper();
    const bool repeated = text.endsWith('*');
    if (repeated) text.chop(1);

    AX25Address addr;
    if (!ax25::splitCallsign(text, addr.call, addr.ssid, error)) return std::nullopt;
    addr.hBit = repeated;
    return addr;
}

QByteArray AX25Address::encode(bool last) const
{
    QByteArray out;
    out.reserve(7);
    const QString padded = call.leftJustified(6, ' ', true);
    for (int i = 0; i < 6; i++) {
        out.append(static_cast<char>((padded.at(i).unicode() & 0x7F) << 1));
    }
    const quint8 ssidOctet = (hBit ? ax25::SsidHMask : 0)
                           | ax25::SsidRrMask                                  // reserved, 1 1
                           | ((ssid << ax25::SsidSsidShift) & ax25::SsidSsidMask)
                           | (last ? ax25::SsidLastMask : 0);
    out.append(static_cast<char>(ssidOctet));
    return out;
}

std::optional<AX25Address> AX25Address::decode(const QByteArray &raw, bool *isLast)
{
    if (raw.size() != 7) return std::nullopt;

    QString call;
    for (int i = 0; i < 6; i++) {
        call.append(QChar((static_cast<unsigned char>(raw.at(i)) >> 1) & 0x7F));
    }
    while (call.endsWith(' ')) call.chop(1);

    const auto ssidOctet = static_cast<unsigned char>(raw.at(6));
    AX25Address addr;
    addr.call = call;
    addr.ssid = (ssidOctet & ax25::SsidSsidMask) >> ax25::SsidSsidShift;
    addr.hBit = (ssidOctet & ax25::SsidHMask) != 0;
    if (isLast) *isLast = (ssidOctet & ax25::SsidLastMask) != 0;
    return addr;
}


// ---- AX25Frame ---------------------------------------------------------------

std::optional<AX25Frame> AX25Frame::ui(const QString &source, const QString &destination,
                                       const QByteArray &info, const QStringList &via,
                                       bool poll, QString *error)
{
    AX25Frame frame;

    for (const QString &v : via) {
        if (v.trimmed().isEmpty()) continue;
        auto digi = AX25Address::parse(v, error);
        if (!digi) return std::nullopt;
        digi->hBit = false;
        frame.digipeaters.append(*digi);
    }
    if (frame.digipeaters.size() > ax25::MaxRepeaters) {
        if (error) *error = QStringLiteral("At most %1 digipeaters allowed, got %2")
                                .arg(ax25::MaxRepeaters).arg(frame.digipeaters.size());
        return std::nullopt;
    }
    if (info.size() > ax25::MaxInfoLen) {
        if (error) *error = QStringLiteral("Information field too long: %1 octets").arg(info.size());
        return std::nullopt;
    }

    auto dest = AX25Address::parse(destination, error);
    if (!dest) return std::nullopt;
    auto src = AX25Address::parse(source, error);
    if (!src) return std::nullopt;

    frame.destination = *dest;
    frame.source = *src;
    frame.control = poll ? ax25::ControlUiPoll : ax25::ControlUi;
    frame.pid = ax25::PidNoLayer3;
    frame.info = info;
    frame.command = true;
    return frame;
}

QByteArray AX25Frame::encode() const
{
    const AX25Address dest(destination.call, destination.ssid, command);
    const AX25Address src(source.call, source.ssid, !command);

    QByteArray out;
    out += dest.encode(false);
    out += src.encode(digipeaters.isEmpty());
    for (int i = 0; i < digipeaters.size(); i++) {
        out += digipeaters.at(i).encode(i == digipeaters.size() - 1);
    }

    out.append(static_cast<char>(control));
    if (pid && isInformationFrame()) {
        out.append(static_cast<char>(*pid));
    }
    out += info;
    return out;
}

std::optional<AX25Frame> AX25Frame::decode(const QByteArray &raw, QString *error)
{
    if (raw.size() < ax25::MinAddresses * 7 + 1) {
        if (error) *error = QStringLiteral("Frame too short: %1 octets").arg(raw.size());
        return std::nullopt;
    }

    QList<AX25Address> addresses;
    int offset = 0;
    bool terminated = false;
    while (offset + 7 <= raw.size()) {
        bool last = false;
        auto addr = AX25Address::decode(raw.mid(offset, 7), &last);
        if (!addr) {
            if (error) *error = QStringLiteral("Address field must be exactly 7 octets");
            return std::nullopt;
        }
        addresses.append(*addr);
        offset += 7;
        if (last) { terminated = true; break; }
        if (addresses.size() > ax25::MaxAddresses) {
            if (error) *error = QStringLiteral("Address field never terminated");
            return std::nullopt;
        }
    }
    if (!terminated) {
        if (error) *error = QStringLiteral("Truncated address field");
        return std::nullopt;
    }
    if (addresses.size() < ax25::MinAddresses) {
        if (error) *error = QStringLiteral("Missing source or destination address");
        return std::nullopt;
    }
    if (offset >= raw.size()) {
        if (error) *error = QStringLiteral("Missing control field");
        return std::nullopt;
    }

    const AX25Address &dest = addresses.at(0);
    const AX25Address &src = addresses.at(1);

    AX25Frame frame;
    frame.destination = AX25Address(dest.call, dest.ssid);
    frame.source = AX25Address(src.call, src.ssid);
    frame.digipeaters = addresses.mid(2);
    frame.control = static_cast<quint8>(raw.at(offset));
    frame.pid = std::nullopt;
    frame.command = dest.hBit && !src.hBit;
    offset += 1;

    if (frame.isInformationFrame()) {
        if (offset >= raw.size()) {
            if (error) *error = QStringLiteral("Missing PID field");
            return std::nullopt;
        }
        frame.pid = static_cast<quint8>(raw.at(offset));
        offset += 1;
    }

    frame.info = raw.mid(offset);
    return frame;
}

QString AX25Frame::frameType() const
{
    const quint8 ctrl = control;

    if ((ctrl & 0x01) == 0) {
        return QStringLiteral("I ns=%1 nr=%2").arg((ctrl >> 1) & 0x07).arg((ctrl >> 5) & 0x07);
    }
    if ((ctrl & 0x03) == 0x01) {
        QString name;
        switch (ctrl & 0x0C) {
            case 0x00: name = QStringLiteral("RR"); break;
            case 0x04: name = QStringLiteral("RNR"); break;
            case 0x08: name = QStringLiteral("REJ"); break;
            case 0x0C: name = QStringLiteral("SREJ"); break;
            default:   name = QStringLiteral("S?"); break;
        }
        return name + QStringLiteral(" nr=%1").arg((ctrl >> 5) & 0x07);
    }

    // Unnumbered frames: mask the P/F bit, except for UI with P/F set
    // which has its own entry.
    const quint8 key = (ctrl == ax25::ControlUiPoll) ? ctrl : (ctrl & 0xEF);
    switch (key) {
        case 0x2F: return QStringLiteral("SABM");
        case 0x6F: return QStringLiteral("SABME");
        case 0x43: return QStringLiteral("DISC");
        case 0x0F: return QStringLiteral("DM");
        case 0x63: return QStringLiteral("UA");
        case 0x87: return QStringLiteral("FRMR");
        case 0x03: return QStringLiteral("UI");
        case 0x13: return QStringLiteral("UI");
        case 0xAF: return QStringLiteral("XID");
        case 0xE3: return QStringLiteral("TEST");
        default:   return QStringLiteral("U 0x%1").arg(QString::number(ctrl, 16).toUpper(), 2, QChar('0'));
    }
}

QString AX25Frame::text(const QString &encoding) const
{
    QString decoded = ax25::decodeText(info, encoding);
    decoded.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    decoded.replace('\r', '\n');

    QString out;
    out.reserve(decoded.size());
    for (const QChar c : decoded) {
        if (c == '\n' || c >= ' ') {
            out.append(c);
        } else {
            out.append(QStringLiteral("<%1>").arg(QString::number(c.unicode(), 16).toUpper(), 2, QChar('0')));
        }
    }
    return out.trimmed();
}

QString AX25Frame::path() const
{
    QStringList parts;
    parts << destination.toString();
    for (const AX25Address &digi : digipeaters) parts << digi.display();
    return parts.join(',');
}

QString AX25Frame::toTnc2(const QString &encoding) const
{
    return QStringLiteral("%1>%2:%3").arg(source.toString(), path(), text(encoding));
}


// ---- Fragmentation --------------------------------------------------------------

namespace ax25 {

QList<QByteArray> splitMessage(const QString &textIn, int maxLen, const QString &encoding)
{
    QList<QByteArray> chunks;

    QString text = textIn;
    text.remove('\r');
    text = text.trimmed();
    if (text.isEmpty()) return chunks;
    if (maxLen < 1) maxLen = 1;

    const QByteArray raw = encodeText(text, encoding);
    if (raw.size() <= maxLen) {
        chunks.append(raw);
        return chunks;
    }

    QString remaining = text;
    while (!remaining.isEmpty()) {
        const QByteArray encoded = encodeText(remaining, encoding);
        if (encoded.size() <= maxLen) {
            chunks.append(encoded);
            break;
        }

        // Take whole characters (surrogate pairs included) while they fit
        // in the octet budget; at least one character always goes.
        QString piece;
        int used = 0;
        int i = 0;
        while (i < remaining.size()) {
            int step = (remaining.at(i).isHighSurrogate() && i + 1 < remaining.size()) ? 2 : 1;
            const QString ch = remaining.mid(i, step);
            const int len = encodeText(ch, encoding).size();
            if (used + len > maxLen && !piece.isEmpty()) break;
            piece += ch;
            used += len;
            i += step;
            if (used >= maxLen) break;
        }

        // Prefer to cut on whitespace so words are not chopped, as long as
        // the piece does not become too short.
        const int space = piece.lastIndexOf(' ');
        if (space > maxLen / 2) {
            piece = piece.left(space);
        }

        chunks.append(encodeText(piece, encoding));
        remaining = remaining.mid(piece.size());
        while (remaining.startsWith(' ')) remaining.remove(0, 1);
    }

    return chunks;
}

} // namespace ax25
