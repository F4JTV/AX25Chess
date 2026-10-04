/*
 * Catalog.cpp - the French translation, compiled in.  See Catalog.h.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Catalog.h"

Catalog::Catalog(QObject *parent)
    : QTranslator(parent)
{
}

QString Catalog::translate(const char *context, const char *sourceText, const char *disambiguation, int n) const
{
    Q_UNUSED(context);
    Q_UNUSED(disambiguation);
    Q_UNUSED(n);
    // An empty result tells Qt to fall back to the source text.
    return french().value(QString::fromUtf8(sourceText));
}

const QHash<QString, QString> &Catalog::french()
{
    static const QHash<QString, QString> table = {
#include "catalog_fr.inc"
    };
    return table;
}
