#pragma once
#include <QObject>
#include <QString>
namespace airlet {
class MainWindow;
class PdfView;
class DocumentTools final : public QObject {
  public:
    DocumentTools(MainWindow *window, PdfView *view);
    ~DocumentTools() override;
    void find();
    void print();
    void imagesPdf();
    bool isBusy() const;

  private:
    class State;
    State *d;
};
} // namespace airlet
