// reidwin.py — Scripts → 🔢 IDs: the mod's event-id system (a model picked once, written into the mod and described
// there) and Re-ID — every event of the files picked numbered anew by it, every call following (reid). One undo step.
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/reid.h"

#include <QWidget>
#include <map>
#include <optional>
#include <utility>
#include <vector>

class QLabel;
class QListWidget;
class QPushButton;
class QTimer;

namespace scriptstore {
class ScriptStore;
}
namespace widgets {
class Stepper;
}

namespace reidwin {

class ReIdWindow : public QWidget {
    Q_OBJECT
public:
    explicit ReIdWindow(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    QString model = QStringLiteral("blocks");
    struct PlanNow {
        OrderedMap<QString, QString> mapping;
        reid::System sys;
        reid::Texts texts;
        reid::Counts cnt;
    };
    std::optional<PlanNow> _plan;
    QTimer *_timer;
    std::map<QString, QPushButton *> seg;
    std::vector<std::pair<QString, QPushButton *>> seg_order;   // (in MODELS' order)
    QLabel *model_note, *block_lbl, *summary, *msg, *doc_view;
    widgets::Stepper *start, *block;
    QListWidget *files;
    QPushButton *go_btn, *sys_btn;
    std::vector<std::pair<QString, long long>> _known_files;

    QLabel *_bold(const QString &text);
    reid::System system() const;
    QStringList picked_files() const;
    void open_for(document::Document *doc);
    void _set_model(const QString &k);
    void _tick_all(bool on);
    void _preview();
    reid::Texts _system_texts(const reid::System &sysd, const reid::Texts *after);
    void _go();
    void _system_only();
Q_SIGNALS:
    void scriptsEdit(const OrderedMap<QString, QString> &changes, const QString &label);   // {file: text}, undo label
};

}  // namespace reidwin
