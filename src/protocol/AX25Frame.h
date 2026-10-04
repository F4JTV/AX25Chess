/*
 * AX25Frame.h - AX.25 v2.2 frames held in decoded form.
 *
 * Port of ax25chess/ax25.py.  Address packing, SSID bit layout and the
 * command/response bits follow ax25_pad.c; the FCS is Direwolf's own
 * fcs_calc(), compiled into direwolf_core.  The test suite checks that
 * AX25Frame::encode() produces exactly the octets ax25_pad.c produces for
 * the same monitor text, so the two cannot drift apart unnoticed.
 *
 * Only UI frames are built by the application; received frames of any type
 * are decoded so the monitor shows real traffic.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace ax25 {

constexpr int MaxRepeaters = 8;
constexpr int MinAddresses = 2;
constexpr int MaxAddresses = 10;
constexpr int MaxInfoLen = 2048;
constexpr int MaxPacketLen = MaxAddresses * 7 + 2 + 3 + MaxInfoLen;

constexpr quint8 ControlUi = 0x03;       // UI, P/F clear
constexpr quint8 ControlUiPoll = 0x13;   // UI, P/F set
constexpr quint8 PidNoLayer3 = 0xF0;

// SSID octet layout, mirroring ax25_pad.h
constexpr quint8 SsidHMask = 0x80;
constexpr quint8 SsidRrMask = 0x60;
constexpr quint8 SsidSsidMask = 0x1E;
constexpr int SsidSsidShift = 1;
constexpr quint8 SsidLastMask = 0x01;

// Frame check sequence, CRC-16/X-25, computed by Direwolf's fcs_calc().
quint16 fcsCalc(const QByteArray &data);
QByteArray fcsAppend(const QByteArray &data);
bool fcsCheck(const QByteArray &dataWithFcs);

// "MYCALL-7" -> ("MYCALL", 7).  A missing SSID gives 0.  Returns false and
// fills error when the callsign or SSID is not legal.
bool splitCallsign(const QString &text, QString &call, int &ssid, QString *error = nullptr);

// "MYCALL" + 7 -> "MYCALL-7"; SSID 0 is omitted, as is customary.
QString formatCallsign(const QString &call, int ssid);

} // namespace ax25


// One AX.25 address: callsign, SSID and the H bit.
class AX25Address
{
public:
    QString call;
    int ssid = 0;
    bool hBit = false;   // C bit for dest/source, has-been-repeated for digis

    AX25Address() = default;
    AX25Address(const QString &call, int ssid, bool hBit = false)
        : call(call), ssid(ssid), hBit(hBit) {}

    // From text; a trailing '*' sets the has-been-repeated flag.
    static std::optional<AX25Address> parse(const QString &text, QString *error = nullptr);

    // The 7 octet on-air form.
    QByteArray encode(bool last) const;

    // Unpack 7 octets.  isLast receives the extension bit.
    static std::optional<AX25Address> decode(const QByteArray &raw, bool *isLast);

    QString toString() const { return ax25::formatCallsign(call, ssid); }

    // Monitor style: appends '*' when the digipeater has repeated it.
    QString display() const { return toString() + (hBit ? QStringLiteral("*") : QString()); }

    bool sameStation(const AX25Address &other) const
    {
        return call == other.call && ssid == other.ssid;
    }
};


class AX25Frame
{
public:
    AX25Address destination;
    AX25Address source;
    QList<AX25Address> digipeaters;   // via path, in transmission order
    quint8 control = ax25::ControlUi;
    std::optional<quint8> pid = ax25::PidNoLayer3;
    QByteArray info;
    bool command = true;              // true -> C bits 1/0, false -> 0/1

    // ---- build ----------------------------------------------------------

    // UI frame with PID 0xF0.  Returns nullopt and fills error on a bad
    // address, too many digipeaters or an oversized information field.
    static std::optional<AX25Frame> ui(const QString &source, const QString &destination,
                                       const QByteArray &info, const QStringList &via = {},
                                       bool poll = false, QString *error = nullptr);

    // ---- encode ----------------------------------------------------------

    // Address field, control, PID and information: what the modem wants.
    // No flags, no bit stuffing, no FCS; the modem adds all of that.
    QByteArray encode() const;

    // Same, with the FCS appended: raw HDLC, not what the modem wants.
    QByteArray encodeWithFcs() const { return ax25::fcsAppend(encode()); }

    // ---- decode ----------------------------------------------------------

    // Parse a frame as delivered by the modem (no FCS).
    static std::optional<AX25Frame> decode(const QByteArray &raw, QString *error = nullptr);

    // ---- accessors --------------------------------------------------------

    bool isUi() const { return (control & ~0x10) == ax25::ControlUi; }

    // I and UI frames carry a PID octet; S and other U frames do not.
    bool isInformationFrame() const { return (control & 0x01) == 0 || isUi(); }

    // Short human readable frame type, for the monitor window.
    QString frameType() const;

    // Information field as text, with unprintable octets made visible.
    QString text(const QString &encoding = QStringLiteral("utf-8")) const;

    // TNC-2 style path: DEST,DIGI1*,DIGI2
    QString path() const;

    // Classic TNC-2 monitor line: SOURCE>DEST,PATH:information
    QString toTnc2(const QString &encoding = QStringLiteral("utf-8")) const;
};


namespace ax25 {

// Break a chat message into information fields no longer than maxLen
// octets, preferring to cut on whitespace so words are not chopped and
// never cutting inside a multi-octet character.  Empty for blank input.
QList<QByteArray> splitMessage(const QString &text, int maxLen = 200,
                               const QString &encoding = QStringLiteral("utf-8"));

// Encode text with the configured encoding ("utf-8" or "ascii"; anything
// else falls back to UTF-8).  Characters the encoding cannot represent
// become '?'.
QByteArray encodeText(const QString &text, const QString &encoding);
QString decodeText(const QByteArray &bytes, const QString &encoding);

} // namespace ax25
