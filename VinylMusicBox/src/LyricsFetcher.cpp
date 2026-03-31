// Stub - full implementation in Task 6
#include <QObject>

class LyricsFetcher : public QObject {
    Q_OBJECT
public:
    explicit LyricsFetcher(QObject *parent = nullptr) : QObject(parent) {}
};
