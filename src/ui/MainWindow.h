#pragma once
#include "export/ImageSaveJob.h"
#include <QMainWindow>
#include <QPointer>
class QLabel;
class QSpinBox;
class QAction;
class QProgressDialog;
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
    ImageSaveJob imageSaver{this};
    QPointer<QProgressDialog> cropProgress;
    QAction *copyAction = nullptr, *saveAreaAction = nullptr;
    quint64 pendingImage = 0;
    QString pendingSavePath, pendingSaveSource;
    QByteArray pendingSaveFormat;
    bool pendingOverwrite = false;
    void chooseFile();
    void copySelection();
    void saveArea();
    void beginAreaOperation();
    void cancelAreaOperation();
    void endAreaOperation();
};
} // namespace airlet
