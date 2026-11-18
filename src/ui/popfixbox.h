// popfixbox.py — Population: the strip at the top that says when the pops have something this mod doesn't — a pop
// type (an error), a culture or religion that isn't defined, a province that doesn't exist — and offers the ways out
// right there (the user, 2026-10-15: "give a warning when it happens and offer two options: import the pop type, or
// change them into a different pop type"). No pop-ups: the strip is part of the page.
// (Python reads win.doc / win.mod / win.refresh_panel; here they are passed in.) Also popcheck.import_from, which needs
// the importer (map layer): it lives here.
#pragma once
#include "core/moddata.h"
#include "core/popcheck.h"

#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <vector>

class QFrame;
class QVBoxLayout;

namespace document {
class Document;
}

namespace popfixbox {

extern const std::map<QString, QString> WHAT;

// popcheck.import_from: import the pop type (culture, religion) `key` from the mod in `folder` — the Import page's own
// way: with what it needs (its name, its icon …), one undo step, written on Apply. -> the message.
QString import_from(moddata::Mod &mod, document::Document &doc, const QString &kind, const QString &key,
                    const QString &folder);

class PopFixBox : public QWidget {
    Q_OBJECT
public:
    PopFixBox(moddata::Mod &mod, std::function<document::Document *()> doc, std::function<void()> refresh_panel,
              QWidget *parent = nullptr);
    moddata::Mod &mod;
    std::function<document::Document *()> doc;
    std::function<void()> refresh_panel;
    QFrame *card;
    std::optional<QString> _sig;
    std::map<std::pair<QString, QString>, std::vector<std::pair<QString, QString>>> _sources;   // (kind, key) -> [(name, folder)]
    long long _last_count = 0;
    popcheck::Problems _probs;
    QVBoxLayout *lay;
    void refresh();
    QWidget *_row(const QString &field, const QString &key, const popcheck::Problem &d);
    QWidget *_prov_row(const OrderedMap<long long, popcheck::Problem> &provs);
    void _import(const QString &field, const QString &key, const QString &folder);
    void _change(const QString &field, const QString &key, const std::optional<QString> &nw);
    void _drop(const QList<long long> &pids);
Q_SIGNALS:
    void message(const QString &text);
    void selectRequested(const QList<long long> &pids);
};

}  // namespace popfixbox
