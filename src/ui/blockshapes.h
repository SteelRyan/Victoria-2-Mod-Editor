// blockshapes.py — How the blocks of the block builder look and fit: pieces that clip into each other like Lego.
//
// Every piece has notches cut into its top edge and a bump under its bottom edge; a piece that holds others is shaped
// like a C — a head, an arm down the left and a lip below — with a bump inside its mouth that says what clips in:
//   • the SHAPE of the bump says what kind of piece: pointed = a condition, flat = an effect, round = a possibility
//     of "exactly one of these";
//   • the PLACE of the bump says whom it is about: left = a nation, middle = a province, right = a pop.
// A piece has a notch for every place it fits (a condition about a province: one notch in the middle; one that fits
// anywhere: all three), so a piece that doesn't belong visibly doesn't sit on the bump.
//
// (A widget is a piece when its property "is_piece" is true — Python's class attribute is_piece.)
#pragma once
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QLayout>
#include <QPainterPath>
#include <QString>
#include <QWidget>
#include <optional>
#include <tuple>
#include <vector>

class QPainter;

namespace blockshapes {

constexpr int NH = 7, NW = 20;                       // a notch / bump: how deep, how wide
int SLOT_AT(QChar k);                                    // where the notch for a nation / a province / a pop sits
constexpr int ARM = 16, LIP = 13, R = 6;              // a C piece: its arm's width, its lip's height; the corners' radius
constexpr int MIN_W = 100, MIN_C = 124;               // the narrowest piece (all three notches fit) / the narrowest C piece
std::tuple<int, int, int> RGB_OF(const QString &kind);   // (an unknown kind: "raw")
extern const QString PICKED;                          // the block that is picked: its edge

std::vector<int> slots(const QString &scopes);        // the notch places of a piece that fits these scopes
std::vector<QPointF> conn(double x, double y, const QString &mode);
struct Mouth {                                        // a C piece: the mouth from y0 down to y1, bumps of mode at places
    double y0, y1;
    std::vector<int> inner;
    QString mode;
};
QPainterPath piece_path(double w, double h, const std::vector<int> &tops = {}, const QString &top_mode = QStringLiteral("eff"),
                        const std::vector<int> &bottoms = {}, const QString &bottom_mode = QStringLiteral("eff"),
                        const std::optional<Mouth> &mouth = std::nullopt);
QColor fill_of(const QString &kind);
QColor edge_of(const QString &kind);
void paint_piece(QPainter *p, const QPainterPath &path, const QString &kind, bool wrong = false, bool lit = false,
                 bool picked = false);

class PieceStack : public QLayout {
    // Pieces under each other, each as wide as it needs (never wider than there is room: then it wraps), each
    // overlapping the bump of the one above — so they sit in each other. Widgets that are no pieces (the "＋" row) take
    // the whole width and keep clear of the bumps.
public:
    explicit PieceStack(QWidget *parent = nullptr, const QMargins &margins = QMargins());
    ~PieceStack() override;
    void addItem(QLayoutItem *item) override;
    void insertWidget(int i, QWidget *w);             // one more piece at place i (the others stay as they are)
    int count() const override;
    QLayoutItem *itemAt(int i) const override;
    QLayoutItem *takeAt(int i) override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int w) const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;
    int _lay(const QRect &rect, bool place) const;

private:
    std::vector<QLayoutItem *> _items;
};

bool is_piece(const QWidget *w);

// ---------------------------------------------------------------------------------------------- a piece, drawn
class Flow {
    // Words and value slots of a sentence, flowed into lines: for the pieces of the list (pictures of blocks).
public:
    static constexpr int PAD_L = 11, PAD_R = 10, LINE = 22;
    struct Place {
        QString kind, text;
        int x, y, cw;
    };
    Flow(const std::vector<std::pair<QString, QString>> &chunks, const QFont &font, const QFont &slot_font);
    std::vector<std::pair<QString, QString>> chunks;  // [("t", word) / ("s", what goes there)]
    QFontMetrics fm, sfm;
    QFont font, slot_font;
    std::vector<Place> places;
    int w = 0, h = 0;
    int width_of(const QString &kind, const QString &text) const;
    std::pair<int, int> lay(int maxw, int min_w = MIN_W);   // -> (width, height of the head)
    void paint(QPainter *p) const;
};

class Legend : public QWidget {
    // What the shapes mean, drawn with the shapes themselves.
public:
    explicit Legend(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *e) override;
};

}  // namespace blockshapes
