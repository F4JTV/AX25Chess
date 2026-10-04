/*
 * Catalog.h - the French translation, compiled in.
 *
 * Why not Qt Linguist: the .ts -> .qm step needs lrelease on every build
 * machine (the Windows kit, Ubuntu's packages, the host tools of the Android
 * build), one more thing to go missing.  A catalog in the code travels with
 * it, is read in a review, and the runtime switch works the same: this
 * translator is installed like any QTranslator, qsTr() and tr() go through
 * it, and the QML engine retranslates on the spot.  The source strings are
 * English; a string missing from the catalog shows in English, never as a
 * technical key.  tests/tst_ui.cpp checks every string of the sources has
 * its translation.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QHash>
#include <QString>
#include <QTranslator>

class Catalog : public QTranslator
{
    Q_OBJECT

public:
    explicit Catalog(QObject *parent = nullptr);

    bool isEmpty() const override { return false; }
    QString translate(const char *context, const char *sourceText, const char *disambiguation = nullptr,
                      int n = -1) const override;

    // English source -> French.
    static const QHash<QString, QString> &french();
};
