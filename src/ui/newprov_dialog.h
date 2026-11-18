// newprov.py, the window half: NewProvinceDialog — name, land / sea, and the province to copy from.
// (The data half — ProvinceDefs, plan, status — is map/newprov.)
#pragma once
#include <QDialog>
#include <map>
#include <optional>
#include <tuple>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QButtonGroup;

namespace moddata {
class Mod;
}

namespace newprov {

class NewProvinceDialog : public QDialog {
    Q_OBJECT
public:
    NewProvinceDialog(const moddata::Mod &mod, long long pid, const std::tuple<int, int, int> &rgb, const QString &where,
                      std::optional<long long> tmpl, QWidget *parent = nullptr);
    const moddata::Mod &mod;
    QLineEdit *name;
    QButtonGroup *kind_group;
    std::map<QString, QPushButton *> kind_btns;
    QLabel *tpl_lbl, *tpl_name, *note, *warn;
    QSpinBox *tpl;
    QDialogButtonBox *buttons;
    bool is_sea() const;
    struct Values {
        QString name;
        bool sea;
        std::optional<long long> tmpl;
    };
    Values values() const;
    void _update();
};

}  // namespace newprov
