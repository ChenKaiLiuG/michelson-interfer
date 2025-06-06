#include "mainWindow.h"
#include "qnamespace.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QVBoxLayout>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

/*
 * 紀錄上一次分析的設定，避免重複分析
 * @field fileName: 影片檔名
 * @field roiSize: ROI方形的邊長
 * @field roiCenter: ROI中心點座標
 */
struct AnalyzeSettings {
  std::string fileName;
  int roiSize;
  QPoint roiCenter;
};

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
  this->showMaximized();
  // Buttons
  btnSelectVideo = new QPushButton("選擇影片", this);
  btnAnalyze = new QPushButton("分析", this);
  btnExport = new QPushButton("匯出", this);
  btnSetRoiSize = new QPushButton("設定ROI大小", this);

  // Inputs
  auto *labelThresholdLow = new QLabel("閾值（低）", this);
  editThresholdLow = new QDoubleSpinBox(this);
  editThresholdLow->setRange(0, 1000);
  editThresholdLow->setValue(50);
  auto *labelThresholdHigh = new QLabel("閾值（高）", this);
  editThresholdHigh = new QDoubleSpinBox(this);
  editThresholdHigh->setRange(0, 1000);
  editThresholdHigh->setValue(200);

  // Chart
  lineSeries = new QtCharts::QLineSeries();
  chart = new QtCharts::QChart();
  chart->addSeries(lineSeries);
  chart->legend()->hide();
  chart->createDefaultAxes();
  auto axesY = chart->axes(Qt::Vertical, lineSeries);
  if (!axesY.isEmpty()) {
    auto *valueAxisY = qobject_cast<QtCharts::QValueAxis *>(axesY.first());
    if (valueAxisY)
      valueAxisY->setRange(0, 255);
  }
  chartView = new QtCharts::QChartView(chart, this);

  // Image display
  graphicsView = new QGraphicsView(this);
  graphicsScene = new QGraphicsScene(this);
  graphicsView->setScene(graphicsScene);
  graphicsView->setMinimumSize(480, 320);
  graphicsView->setStyleSheet(
      "background: #eee; border: 1px solid #ccc; color: #888;");
  imageItem = nullptr;
  roiCrossItem = nullptr;
  roiRectItem = nullptr;
  roiCenter = QPoint(-1, -1);
  // Sliders
  frameSlider = new QSlider(Qt::Horizontal, this);
  frameSlider->setMinimum(0);
  frameSlider->setMaximum(0);
  frameSlider->setTickPosition(QSlider::TicksBelow);

  rangeSliderMin = new QSlider(Qt::Horizontal, this);
  rangeSliderMin->setMinimum(0);
  rangeSliderMin->setMaximum(0);

  rangeSliderMax = new QSlider(Qt::Horizontal, this);
  rangeSliderMax->setMinimum(0);
  rangeSliderMax->setMaximum(0);
  // Slider labels
  labelRangeMin = new QLabel("分析範圍起點: 0", this);
  labelRangeMin->setStyleSheet("font-size: 20px;");
  labelRangeMax = new QLabel("分析範圍終點: 0", this);
  labelRangeMax->setStyleSheet("font-size: 20px;");
  labelFrame = new QLabel("目前影格: 0", this);
  labelFrame->setStyleSheet("font-size: 20px;");

  // Result display
  labelCircleChange = new QLabel("圓形變化 : N/A", this);
  labelCircleChange->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  // Layouts
  // 上方的三個按鈕
  auto *buttonLayout = new QHBoxLayout();
  buttonLayout->addWidget(btnSelectVideo);
  buttonLayout->addWidget(btnAnalyze);
  buttonLayout->addWidget(btnExport);

  auto *leftLayout = new QVBoxLayout();
  leftLayout->addLayout(buttonLayout);
  leftLayout->addWidget(labelThresholdLow);
  leftLayout->addWidget(editThresholdLow);
  leftLayout->addWidget(labelThresholdHigh);
  leftLayout->addWidget(editThresholdHigh);
  leftLayout->addWidget(btnSetRoiSize);
  leftLayout->addStretch();
  leftLayout->addWidget(labelCircleChange);

  auto *rightLayout = new QVBoxLayout();
  rightLayout->addWidget(graphicsView);
  rightLayout->addWidget(labelRangeMin);
  rightLayout->addWidget(rangeSliderMin);
  rightLayout->addWidget(labelRangeMax);
  rightLayout->addWidget(rangeSliderMax);
  rightLayout->addWidget(labelFrame);
  rightLayout->addWidget(frameSlider);

  auto *mainLayout = new QVBoxLayout(this);
  auto *topLayout = new QHBoxLayout();
  topLayout->addLayout(leftLayout);
  topLayout->addLayout(rightLayout);
  mainLayout->addLayout(topLayout);

  auto *contentLayout = new QHBoxLayout();
  contentLayout->addWidget(chartView, 0, Qt::AlignTop);
  chartView->setFixedHeight(540);
  mainLayout->addLayout(contentLayout);

  setLayout(mainLayout);

  qRegisterMetaType<cv::Mat>("cv::Mat");
  // File I/O & Analyzer
  fileIo = new FileIo(this);
  analyzer = new Analyzer(this);
  // connect signals and slots
  connect(fileIo, &FileIo::loadFrame, analyzer, &Analyzer::calculateMean);
  connect(analyzer, &Analyzer::updateResults, this, &MainWindow::updateResults);
  connect(btnSelectVideo, &QPushButton::clicked, this,
          &MainWindow::selectVideo);
  connect(btnAnalyze, &QPushButton::clicked, this, &MainWindow::analyze);
  connect(btnExport, &QPushButton::clicked, this, &MainWindow::exportCsv);
  connect(btnSetRoiSize, &QPushButton::clicked, this,
          &MainWindow::showRoiSizeDialog);
  // Connect slider signals to update labels
  connect(rangeSliderMin, &QSlider::valueChanged, this,
          [this](int min) { this->updateSlider(min, -1, -1); });
  connect(rangeSliderMax, &QSlider::valueChanged, this,
          [this](int max) { this->updateSlider(-1, max, -1); });
  connect(frameSlider, &QSlider::valueChanged, this,
          [this](int value) { this->updateSlider(-1, -1, value); });
}

MainWindow::~MainWindow() {}

/*
 * 更新圖表和圖片
 * @param frame: 當前幀
 * @param meanIntesity: 平均強度值的陣列
 * @param startFrame: 開始幀
 * @param size: 平均強度陣列的大小
 */
void MainWindow::updateResults(const cv::Mat &frame, double *meanIntesity,
                               int startFrame, int size) {
  for (int i = 0; i < size; i++) {
    lineSeries->append(startFrame + i, meanIntesity[i]);
  }
  QImage img(frame.data, frame.cols, frame.rows, frame.step[0],
             QImage::Format_BGR888);
  if (imageItem)
    graphicsScene->removeItem(imageItem);
  imageItem = graphicsScene->addPixmap(QPixmap::fromImage(img));
  graphicsScene->setSceneRect(img.rect());
  // 若有 ROI，重畫標記
  if (roiCenter.x() >= 0 && roiCenter.y() >= 0)
    setRoiCenter(roiCenter);
}

/*
 * 選擇影片檔案
 */
void MainWindow::selectVideo() {
  // 開啟檔案選擇對話框
  this->fileName = QFileDialog::getOpenFileName(this, tr("選擇影片"), "",
                                                tr("影片檔案 (*.mp4)"))
                       .toStdString();
  if (fileName.empty()) {
    return;
  }
  // set the image label to the first frame
  cv::Mat firstFrame = fileIo->getFrame(fileName, 0);
  QImage img(firstFrame.data, firstFrame.cols, firstFrame.rows,
             firstFrame.step[0], QImage::Format_BGR888);
  if (imageItem)
    graphicsScene->removeItem(imageItem);
  imageItem = graphicsScene->addPixmap(QPixmap::fromImage(img));
  graphicsScene->setSceneRect(img.rect());
  if (roiCenter.x() >= 0 && roiCenter.y() >= 0)
    updateRoi();
  // Clear the chart and set the range of the x-axis
  lineSeries->clear();
  int frameCount = fileIo->getFrameCount(fileName);
  auto axesX = chart->axes(Qt::Horizontal, lineSeries);
  if (!axesX.isEmpty()) {
    auto *valueAxisX = qobject_cast<QtCharts::QValueAxis *>(axesX.first());
    if (valueAxisX)
      valueAxisX->setRange(0, frameCount);
  }
  // Set the range of the sliders
  frameSlider->setMaximum(frameCount - 1);
  rangeSliderMin->setMaximum(frameCount - 1);
  rangeSliderMax->setMaximum(frameCount - 1);
  rangeSliderMax->setValue(frameCount - 1);
}

/*
 * 分析影片
 */
void MainWindow::analyze() {
  // 上一次的設定
  static AnalyzeSettings prevSettings;
  // Check if the file name is empty or ROI is not set
  if (fileName.empty()) {
    QMessageBox::warning(this, tr("錯誤"), tr("請先選擇影片檔案"));
    return;
  }
  if (roiCenter.x() < 0 || roiCenter.y() < 0) {
    QMessageBox::warning(this, tr("錯誤"), tr("請先設定ROI"));
    return;
  }
  // Get threshold values
  double thresholdLow = editThresholdLow->value();
  double thresholdHigh = editThresholdHigh->value();
  // Check if the threshold values are valid
  if (thresholdLow >= thresholdHigh) {
    QMessageBox::warning(this, tr("錯誤"), tr("閾值（低）必須小於閾值（高）"));
    return;
  }
  // If the settings not changed, do not re-analyze
  if (prevSettings.fileName != fileName || prevSettings.roiSize != roiSize ||
      prevSettings.roiCenter != roiCenter) {
    // Clear previous results
    lineSeries->clear();
    analyzer->clearResults();
    // Load video and start analysis
    fileIo->readFramesAsync(fileName);
  }

  int circleChange = analyzer->calculateCircleChange(
      thresholdLow, thresholdHigh, rangeSliderMin->value(),
      rangeSliderMax->value());

  labelCircleChange->setText(QString("圓形變化 : %1").arg(circleChange));

  // update the prevSettings
  prevSettings.fileName = fileName;
  prevSettings.roiSize = roiSize;
  prevSettings.roiCenter = roiCenter;
}

/*
 * 匯出結果
 */
void MainWindow::exportCsv() {
  // Export the results to a CSV file
  QString fileName = QFileDialog::getSaveFileName(this, tr("匯出結果"), "",
                                                  tr("CSV檔案 (*.csv)"));
  if (!fileName.isEmpty()) {
    fileIo->writeCsv(fileName.toStdString(), this->analyzer->getResults());
  }
}
/*
 * 更新 ROI 標記
 */
void MainWindow::updateRoi() {
  // 移除舊的 ROI 標記
  if (roiCrossItem) {
    graphicsScene->removeItem(roiCrossItem);
    delete roiCrossItem;
    roiCrossItem = nullptr;
  }
  if (roiRectItem) {
    graphicsScene->removeItem(roiRectItem);
    delete roiRectItem;
    roiRectItem = nullptr;
  }
  // 畫紅色 X
  QPainterPath crossPath;
  crossPath.moveTo(roiCenter.x() - 10, roiCenter.y() - 10);
  crossPath.lineTo(roiCenter.x() + 10, roiCenter.y() + 10);
  crossPath.moveTo(roiCenter.x() - 10, roiCenter.y() + 10);
  crossPath.lineTo(roiCenter.x() + 10, roiCenter.y() - 10);
  roiCrossItem = graphicsScene->addPath(crossPath, QPen(Qt::red, 2));
  // 畫紅色矩形
  roiRectItem = graphicsScene->addRect(QRectF(roiCenter.x() - roiSize / 2,
                                              roiCenter.y() - roiSize / 2,
                                              roiSize, roiSize),
                                       QPen(Qt::red, 2), QBrush(Qt::NoBrush));
  // 設定 ROI 的範圍
  analyzer->setBound(roiCenter.x() - roiSize / 2, roiCenter.x() + roiSize / 2,
                     roiCenter.y() - roiSize / 2, roiCenter.y() + roiSize / 2);
}
/*
 * 設定 ROI 位置
 * @param pt: ROI 中心點座標
 */
void MainWindow::setRoiCenter(const QPoint &pt) {
  roiCenter = pt;
  updateRoi();
}
/*
 * 顯示 ROI 大小設定對話框
 */
void MainWindow::showRoiSizeDialog() {
  bool ok = false;
  int value = QInputDialog::getInt(
      this, tr("設定ROI大小"), tr("請輸入ROI邊長："), roiSize, 1, 1000, 1, &ok);
  if (ok) {
    roiSize = value;
    updateRoi();
  }
}
/*
 * 滑鼠點擊事件
 * @param event: 滑鼠事件
 */
void MainWindow::mousePressEvent(QMouseEvent *event) {
  if (graphicsView->underMouse()) {
    // 當滑鼠和shift鍵同時按下時，設定 ROI 中心點
    if ((event->button() == Qt::LeftButton) &&
        (event->modifiers() & Qt::ShiftModifier)) {
      QPoint viewPos = graphicsView->mapFromGlobal(event->globalPos());
      QPointF scenePos = graphicsView->mapToScene(viewPos);
      if (imageItem && imageItem->contains(scenePos)) {
        setRoiCenter(scenePos.toPoint());
      }
    }
  }
  QWidget::mousePressEvent(event);
}
/*
 * 更新滑桿的值
 * @param min: 最小值, -1表示不更新
 * @param max: 最大值, -1表示不更新
 * @param value: 當前值, -1表示不更新
 */
void MainWindow::updateSlider(int min, int max, int value) {
  if (min >= 0) {
    if (min > rangeSliderMax->value()) {
      rangeSliderMin->setValue(rangeSliderMax->value());
    }
    if (min > frameSlider->value()) {
      frameSlider->setValue(min);
      labelFrame->setText(QString("目前影格: %1").arg(min));
    }
    labelRangeMin->setText(QString("分析範圍起點: %1").arg(min));
  }
  if (max >= 0) {
    if (max < rangeSliderMin->value()) {
      rangeSliderMax->setValue(rangeSliderMin->value());
    }
    if (max < frameSlider->value()) {
      frameSlider->setValue(max);
      labelFrame->setText(QString("目前影格: %1").arg(max));
    }
    labelRangeMax->setText(QString("分析範圍終點: %1").arg(max));
  }
  if (value >= 0) {
    if (value < rangeSliderMin->value()) {
      value = rangeSliderMin->value();
    }
    if (value > rangeSliderMax->value()) {
      value = rangeSliderMax->value();
    }
    frameSlider->setValue(value);
    labelFrame->setText(QString("目前影格: %1").arg(value));
    cv::Mat firstFrame = fileIo->getFrame(fileName, value);
    QImage img(firstFrame.data, firstFrame.cols, firstFrame.rows,
               firstFrame.step[0], QImage::Format_BGR888);
    if (imageItem)
      graphicsScene->removeItem(imageItem);
    imageItem = graphicsScene->addPixmap(QPixmap::fromImage(img));
    graphicsScene->setSceneRect(img.rect());
    if (roiCenter.x() >= 0 && roiCenter.y() >= 0)
      updateRoi();
  }
}
