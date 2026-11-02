#pragma once
#include <QMainWindow>
class QLabel;
class QSpinBox;
namespace airlet {
class PdfView;
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openFile(const QString &path);
    PdfView *pdfView() const {
        return view;
    }

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    PdfView *view;
    QSpinBox *page;
    QLabel *count;
    QString lastFolder;
    QString openedPath;
    void chooseFile();
};
} // namespace airlet
