#pragma once
#include "core/FolderNavigator.h"
#include "export/ImageSaveJob.h"
#include <QMainWindow>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <memory>
class QLabel;
class QSpinBox;
class QAction;
class QProgressDialog;
namespace airlet {
class PdfView;
class DocumentTools;
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openFile(const QString &path);
    ~MainWindow() override;
    PdfView *pdfView() const {
        return view;
    }

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    PdfView *view;
    DocumentTools *tools;
    FolderNavigator folderNavigator{this};
    QAction *continueAction = nullptr;
    quint64 pendingFolderRequest = 0;
    bool automaticOpening = false, openPrevious = false;
    int pendingFolderDirection = 1;
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
    QProcess exportProcess;
    std::unique_ptr<QTemporaryDir> exportTemporary;
    QPointer<QProgressDialog> exportProgress;
    QByteArray exportBuffer;
    int exportCompleted = 0;
    QString exportError;
    bool exportCancelled = false;
    void exportPages();
    void chooseFile();
    void openDocument(const QString &path, bool startAtEnd);
    void checkUpdates();
    QProcess updateProcess;
    std::unique_ptr<QTemporaryDir> updateTemporary;
    QByteArray updateBuffer;
    void copySelection();
    void saveArea();
    void beginAreaOperation();
    void cancelAreaOperation();
    void endAreaOperation();
};
} // namespace airlet
