// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QMainWindow>
#include <QStringList>
#include <QHash>
#include <functional>
#include <atomic>
#include <memory>

#include "io/delivery_report.hpp"
#include "runtime/run_controller.hpp"
#include "ui/views/image_view.hpp"
#include "model/spatial_data.hpp"

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QDockWidget;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;
class QLineEdit;
class QScrollArea;
class QTabWidget;
class QTabBar;
class QToolButton;
class QTreeWidgetItem;
class QMenuBar;
class QMenu;
class QToolBar;
class QTimer;
class QSplitter;
class QVBoxLayout;
class QUndoStack;
namespace workbench {
class StudioEditCommand;
class WorkflowView;
class ModelView;
class ElidedLabel;
class ResourceManager;
class MainWindow final : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(bool persistSettings = true, QWidget* parent = nullptr,
                      const QString& workerProgram = {},
                      bool startInWorkflow = true);
  ~MainWindow() override;
  bool openPath(const QString& path, bool project = false, bool promoteLegacyInputs = false);
  bool saveTo(const QString& path, bool copyOriginalData = false);
  bool openRelocatedProject(const QString& path,const QString& directory);
  bool run();
  bool runSelected();
  bool runNext();
  bool loadModel(const QString& path);
  QVector<DeliveryRecord> deliveryRecords(bool selectedOnly = false) const;
  QString exportResults(const QString& parentDirectory, bool annotated = true, bool selectedOnly = false);
  void showBatchResults();
  void showCalibrationWizard();
  void showExampleProjects();
  bool openExampleProject(const QString& id);
  bool addStereoPairs(const QStringList& left, const QStringList& right,
                      const QString& calibration = {});
  void showStereoImport();
    bool addStructuredSequence(const QStringList& paths, const QString& model = {});
    void showStructuredImport();
    void showStructuredGenerator();
  void showCaptureManager();
  void showSampleInputs();
  bool setSampleInput(int sample, const QString& binding, const QStringList& paths, bool sequence);
    void showStructuredSequence(int index = -1, QWidget* browserParent = nullptr);
  bool renameCapture(int index, const QString& name);
  bool replaceStereoCalibration(const QVector<int>& indices, const QString& path);
  bool exportObservedPointCloud(const QString& path);
  bool loadCalibrationArtifact(const QString& path);
  bool setCalibrationBoard(const QString& path);
  bool importTemplate(const QString& path);
  bool exportTemplate(const QString& path, bool nativeModel = false);
  bool debugNode(const QString& id);
  bool selectBasicOperator(Algorithm tool);
  bool renameWorkflowNode(const QString& id, const QString& name);
  QStringList workflowNodeIds() const { return workflowIds_; }
  QString debugNodeId() const { return debugNodeId_; }
  bool addImages(const QStringList& paths);
  bool activateImage(int index);
  void removeImages(bool clear = false);
  int imageCount() const { return images_.size(); }
  const Document& document() const { return document_; }
  const Result& result() const { return result_; }
  bool busy() const;
  quint64 revision() const { return revision_; }
  QString lastError() const { return lastError_; }
  ImageView* imageView() const { return view_; }
  NodeSettings rangeForNode(int index) const {
    const auto settings = document_.rangeOverrides.value(index)
               ? document_.nodeSettings.value(index)
               : sharedRanges_.value(index);
    return resolveNodeInputs(workflow_, workflowLinks_, index, settings,
                             models_);
  }
  const QVector<ModelResource>& modelResources() const { return models_; }
 signals:
  void resultApplied();

 protected:
  void closeEvent(QCloseEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void changeEvent(QEvent* event) override;
  bool nativeEvent(const QByteArray& type, void* message,
                   long* result) override;

 private:
  friend class StudioEditCommand;
  friend class ResourceManager;
  void showResourceManager(const QString& binding = {}, int sample = -1);
  bool setSampleInputs(const QVector<int>& samples, const QString& binding,
                       const SourceData& sourceCell);
  struct ImageResource;
  void buildUi();
  void loadResources(const QString& title,const std::function<void(const std::atomic_bool&)>& operation);
  bool loading_ = false;
  std::shared_ptr<std::atomic_bool> loadingCancel_;
  void showSequenceSnapshot(const Document& snapshot,QWidget* parent);
  bool nodeContext() const {
    return workflowMode_ || !debugNodeId_.isEmpty();
  }
  ImageView* activeImageView() const {
    return workflowMode_ ? nodeView_ : view_;
  }
  QStringList nodeNames() const;
  int activeInputNode() const;
  QVector<int> inputGroupNodes() const;
  bool confirmInputMerge(int source,int target);
  QVector<int> trackSamples(bool wholeGroup=false) const;
  int inputGroupTarget() const;
  void syncInputTrack();
  bool importSourceSamples(const QVector<SourceData>& cells,const QStringList& names);
  void deleteManagedSamples(const QVector<int>& rows);
  QString activeInputKey_;
  bool syncingInputTrack_=false;
  QComboBox* inputTrackChoice_=nullptr;
  QHash<QString,QString> inputPreviewViews_, preferredInputs_;
  int inputPreviewFrame(const SourceData& cell,const QString& binding) const;
  void buildNodeLibrary(QWidget* libraryPage, QDockWidget* resources);
  void refreshNodeLibrary();
  void refreshOperatorSearch();
  void refreshContext();
  void normalizeModelOwnership();
  void samplePixel(ImageView* target, int row, int col);
  void requestNodeRename(int index);
  void buildMenus();
  void rememberProject(const QString& path);
  void refreshRecentProjects();
  QJsonObject projectConfiguration(const QString& path);
  void installProjectConfiguration(const QJsonObject& object,const QString& path);
  void setupSpatialUi();
  void refreshSpatialView(const std::shared_ptr<const SpatialData>& data);
  void selectSpatialRaster();
  void showResidualDisplay();
  double residualTolerance_ = .1;
  bool residualOnlyOutside_ = false;
    void showDepthScale();
    ScalarRange depthScaleRange(int mode, int* ready = nullptr, int* selected = nullptr);
    ScalarRange cachedSpatialRange(const std::shared_ptr<const SpatialData>& data);
  std::shared_ptr<const SpatialData> spatialView_;
  QComboBox *spatialChoice_ = nullptr, *spatialWorkbenchChoice_ = nullptr;
    QComboBox *spatialColor_ = nullptr, *spatialWorkbenchColor_ = nullptr;
    QWidget *spatialHeader_ = nullptr, *spatialWorkbenchHeader_ = nullptr;
    ElidedLabel *spatialLegend_ = nullptr, *spatialWorkbenchLegend_ = nullptr;
    QMap<QString, SpatialPreview> spatialPreviews_;
    int depthScaleMode_ = 0;
    ScalarRange manualDepthScale_{0, 2, true};  // m; UI uses mm.
    QToolButton *depthScaleButton_ = nullptr, *workbenchDepthScaleButton_ = nullptr;
    struct CachedDepthRange { std::weak_ptr<const SpatialData> data; ScalarRange range; };
    QMap<quintptr, CachedDepthRange> depthRanges_;
  QSplitter* spatialViews_ = nullptr;
  void rememberModeLayout();
  void restoreModeLayout();
  QByteArray modeLayouts_[2], modeViewSplits_[2];
  QByteArray modeSpatialSplits_[2];
  bool modeLayoutReady_ = false;
  ModelView* nodeModelView_ = nullptr;
  void buildParameters(QWidget* parent);
  void showParameterPage(int index);
  void buildGeometryUi();
  void syncGeometryUi();
  void bindGeometryInput(int target, int source, bool second);
  void bindNamedInput(int target, const QString& slot, int source, const QString& output);
  void chooseNamedInput(int target, const QString& slot, int source);
  void fillNamedInput(QComboBox* box, int target, const QString& slot);
  Parameters portParameters(int node) const;
  void resetOperatorParameters();
  void updateParameterGuidance();
  void refreshRangeContext();
  void selectResultObject(int index, bool focus = false);
  int selectedResultObject() const;
  void refreshDiagnostics();
  void locateIssue();
  QToolButton* locateIssue_ = nullptr;
  int issueNode_ = -3;
  QString issueField_;
  void newProject();
  void stopExecution();
  void installDocument(Document document, const QString& project);
  void syncParameters();
  void parametersChanged();
  void invalidate();
  void applyDocumentRoi(const Roi& roi);
  Roi currentNodeRoi() const;
  void syncNodeSettings();
  void changeNodeScope(int choice);
  void buildRangeUi(QVBoxLayout* layout, QWidget* parent);
  NodeSettings effectiveRange() const;
  NodeSettings& editableRange();
  void rangeChanged(bool modelChange = false);
  void prepareSnapshot(Document& snapshot) const;
  bool trainingView() const;
  ModelResource* currentModel();
  const ModelResource* currentModel() const;
  void setReferenceImage(const QString& path = {}, int resource = -1);
  void updateRangeView();
  void applyTrainedModels(const Result& result);
  void migrateRanges(QVector<ImageResource>& images,
                     const QVector<Parameters>& flow, int active,
                     const QString& path);
  void refreshProperties();
  void setDirty(bool dirty);
  void log(const QString& text);
  bool guard(const QString& context, const std::function<void()>& action,
             bool silent = false);
  void setOperatorStatus(const QString& state, const char* kind,
                         const QString& detail = {});
  bool beginBatch(bool single);
  bool confirmDiscard();
  void chooseImage();
  void chooseProject();
  void chooseSave(bool saveAs = false);
  void restoreDefaultLayout();
  QByteArray migrateLegacyLayout(const QByteArray& state);
  void updateActions();
  void refreshFilm();
  void storeActive();
  void activateImageAndRun(int index);
  void showResult(const Result& result);
  void inspectResult(const Result& result);
  void refreshExecutionTiming(bool clear = false);
  void refreshNodeOutput();
  int viewNodeIndex() const;
  bool viewPinnedElsewhere() const;
  void refreshViewIdentity();
  Result resultForNode(int index) const;
  QString pinnedViewNode_;
  bool compareNodeInput_ = true;
  QToolButton *pinView_ = nullptr, *floatView_ = nullptr,
              *compareSource_ = nullptr;
  bool startExecution(int throughStep);
  bool launchSnapshot(Document snapshot, int owner);
  void schedulePreview();
  void startNextImage();
  void updateViews();
  void selectImages(bool selected);
  void rememberTool(Algorithm tool, bool multiView = false);
  void setResultState(const QString& text, const char* state);
  void setWorkflowMode(bool enabled);
  void refreshWorkflow();
  void setNodeViewFloating(bool floating);
  void addWorkflowNode(Algorithm tool, QPointF position, int insertTarget = -1,
                       int insertSource = -3, bool multiView = false);
  void removeWorkflowNode(int index);
  void connectWorkflowNode(int target, int source);
  void rewireWorkflowConnection(int oldTarget, int oldSource, int target,
                                int source, const QString& role);
  void disconnectWorkflowNode(int target, int source);
  void selectWorkflowInput(int target, int source);
  void buildImageLayers(ImageView* target);
  void buildImageNavigation(ImageView* target);
  void workflowChanged();
  void refreshInputChoices();
  void syncViewTools();
  struct ImageResource {
    QString id;
    Document document;
    Result result;
    quint64 version = 0;
    bool checked = false;
    quint64 resultVersion = 0;
    Result workflowResult;
    quint64 workflowResultVersion = 0;
    int batchState = 0;
    quint64 baseVersion = 0;
    Result deliveryResult;
    QString deliveryUtc;
    quint64 deliveryVersion = 0;
    bool deliveryWorkflow = false;
    QJsonObject deliveryProvenance;
  };
  struct EditState {
    Document document;
    QVector<ImageResource> images;
    QVector<Parameters> workflow;
    QStringList ids, aliases;
    QVector<NodeSettings> sharedRanges, basicRanges;
    QVector<ModelResource> models;
    QVector<int> inputs;
    QVector<QVector<int>> links;
    QVector<QPointF> positions;
    Parameters basicParameters;
    QString debugId, inputKey;
    QHash<QString,QString> preferredInputs;
    int active = -1, selected = -1, output = -2, role = 0, matchingTab = 0;
    bool workflowMode = true;
    QByteArray key;
  };
  class EditGuard {
   public:
    EditGuard(MainWindow& owner, const QString& text,
              const QString& merge = {});
    ~EditGuard();
    EditGuard(const EditGuard&) = delete;
    EditGuard& operator=(const EditGuard&) = delete;
   private:
    MainWindow& owner_;
    std::shared_ptr<EditState> before_;
    QString text_, merge_;
  };
  std::shared_ptr<EditState> captureEditState() const;
  void restoreEditState(const EditState& state);
  void resetEditHistory();
  QString editMergeKey(const QString& field) const;
  QUndoStack* editHistory_ = nullptr;
  QAction *undoAction_ = nullptr, *redoAction_ = nullptr;
  int editDepth_ = 0;
  quint64 editNavigation_ = 0;
  bool restoringEdit_ = false;
  std::unique_ptr<EditGuard> nodeMoveEdit_;
  using BatchItem=RunItem;
  RunController runQueue_;
  std::shared_ptr<QTemporaryDir> batchCalibrationDirectory_;
  quint64 pendingQueueGeneration_=0;
  QJsonObject pendingProvenance_;
  QTimer* autoPreview_ = nullptr;
  bool batchSwitch_ = false;
  QString lastSingleImage_, pendingOperator_;
  QVector<ImageResource> images_;
  int activeImage_ = -1;
  QString pendingImage_;
  quint64 pendingVersion_ = 0;
  bool pendingWorkflow_ = false;
  QVector<Parameters> workflow_;
  QStringList workflowIds_, workflowAliases_;
  QString debugNodeId_;
  QVector<NodeSettings> sharedRanges_;
  QWidget* geometryInputPanel_ = nullptr;
  QLabel *calibrationResourceLabel_ = nullptr,
         *calibrationBoardLabel_ = nullptr;
  QComboBox *geometryA_ = nullptr, *geometryB_ = nullptr;
  QVector<QPair<QWidget*, QString>> geometryFields_;
  QVector<NodeSettings> workbenchRanges_ = QVector<NodeSettings>(toolSlotCount);
  QVector<ModelResource> models_;
  QVector<int> workflowInputs_;
  QVector<QVector<int>> workflowLinks_;
  QVector<QPointF> workflowPositions_;
  int workflowOutput_ = -2;
  Parameters workbenchParameters_;
  int selectedStep_ = -1;
  bool workflowMode_ = false, dark_ = false;
  WorkflowView* workflowView_ = nullptr;
  QStackedWidget* centerPages_ = nullptr;
  QWidget* imagePane_ = nullptr;
  QSplitter* views_ = nullptr;
  ModelView* modelView_ = nullptr;
  QAction *view2d_ = nullptr, *view3d_ = nullptr;
  QTreeWidget* library_ = nullptr;
  QSplitter* librarySplit_ = nullptr;
  QTreeWidget* nodeLibrary_ = nullptr;
  QWidget* nodeLibraryPanel_ = nullptr;
  QLabel *operatorsTitle_ = nullptr;
  QScrollArea* filmScroll_ = nullptr;
  QLabel *filmCount_ = nullptr, *operatorTitle_ = nullptr,
         *projectCaption_ = nullptr;
  QLabel* workbenchTiming_ = nullptr;
  QString timingText_,
      operatorStatusText_ = QStringLiteral("算子状态 · 就绪");
  QLabel* parameterInput_ = nullptr;
  QComboBox* poseIteration_ = nullptr;
  QDoubleSpinBox *roiAngle_ = nullptr, *roiInner_ = nullptr;
  QToolButton* resetParameters_ = nullptr;
  QWidget *nodeOutputPanel_ = nullptr, *operatorControls_ = nullptr,
          *canvasHeader_ = nullptr;
  ImageView* nodeView_ = nullptr;
  QLabel *nodeOutputType_ = nullptr, *operatorDescription_ = nullptr,
         *advancedDetails_ = nullptr, *resultState_ = nullptr;
  QTabBar* centerTab_ = nullptr;
  QTreeWidgetItem* recentGroup_ = nullptr;
  QVector<QPair<Algorithm, bool>> recentTools_;
  Result inspectedResult_;
  mv::MvImage nodeInputImage_, nodeProcessedImage_;
  QPainterPath nodeInputDomain_;
  QDockWidget* nodeOutputDock_ = nullptr;
  QDockWidget* outputDock_ = nullptr;
  QTabWidget* outputTabs_ = nullptr;
  QTabWidget* nodeViewTabs_ = nullptr;
  QScrollArea* operatorScroll_ = nullptr;
  QComboBox* nodeInput_ = nullptr;
  QComboBox* nodeSearch_ = nullptr;
  QComboBox *nodeScope_ = nullptr, *roiPurpose_ = nullptr,
            *workflowOutputChoice_ = nullptr;
  QTabBar* matchingTabs_ = nullptr;
  QWidget *rangePanel_ = nullptr, *templatePanel_ = nullptr,
          *rangeEditPanel_ = nullptr;
  QComboBox *regionInput_ = nullptr, *poseInput_ = nullptr,
            *positionMode_ = nullptr, *modelChoice_ = nullptr,
            *referenceChoice_ = nullptr;
  QCheckBox* rangeOverride_ = nullptr;
  QPushButton *restoreRange_ = nullptr, *editRange_ = nullptr,
              *clearRange_ = nullptr, *addExclusion_ = nullptr,
              *removeExclusion_ = nullptr, *referencePose_ = nullptr;
  QSpinBox* poseInstance_ = nullptr;
  QDoubleSpinBox *buildStart_ = nullptr, *buildExtent_ = nullptr;
  QLabel *referenceInfo_ = nullptr, *modelStatus_ = nullptr;
  QWidget* templateFileActions_ = nullptr;
  QPushButton *loadTemplate_ = nullptr, *saveTemplate_ = nullptr;
  QTableWidget* variables_ = nullptr;
  QComboBox *filterType_ = nullptr, *grayType_ = nullptr;
  QSpinBox *mask_ = nullptr, *matches_ = nullptr;
  QDoubleSpinBox *score_ = nullptr, *angleStart_ = nullptr,
                 *angleExtent_ = nullptr;
  QLineEdit* templatePath_ = nullptr;
  QToolButton *workbenchMode_ = nullptr, *workflowModeButton_ = nullptr;
  QToolButton* maximizeButton_ = nullptr;
  QMenuBar* mainMenu_ = nullptr;
  QToolBar* mainToolbar_ = nullptr;
  Document document_;
  Result result_;
  ExecutionService execution_;
  quint64 revision_ = 0;
  bool persistSettings_, syncing_ = false, dirty_ = false;
  QString projectPath_, lastError_;
  bool copyOriginalDataOnSave_ = false;
  QByteArray defaultLayout_;
  ImageView* view_ = nullptr;
  QTableWidget* results_ = nullptr;
  QComboBox* calibrationDetails_ = nullptr;
  QPlainTextEdit* logs_ = nullptr;
  QComboBox* algorithm_ = nullptr;
  QWidget* parameterPages_ = nullptr;
  QVBoxLayout* parameterLayout_ = nullptr;
  QVector<QWidget*> parameterForms_;
  QVector<QWidget *> parameterInputForms_, parameterAdvancedForms_;
  QWidget *advancedPages_ = nullptr;
  QComboBox *thresholdMethod_ = nullptr, *thresholdPolarity_ = nullptr;
  QSpinBox* thresholdWindow_ = nullptr;
  QDoubleSpinBox* thresholdOffset_ = nullptr;
  QDoubleSpinBox *minimum_ = nullptr, *maximum_ = nullptr,
                 *opening_ = nullptr, *area_ = nullptr, *sigma_ = nullptr;
  QSpinBox *low_ = nullptr, *high_ = nullptr;
  QLabel *imageTitle_ = nullptr, *imageInfo_ = nullptr, *roiInfo_ = nullptr,
         *objectInfo_ = nullptr;
  QLabel *operatorStatus_ = nullptr, *zoomStatus_ = nullptr,
         *taskStatus_ = nullptr, *summary_ = nullptr, *validation_ = nullptr;
  QProgressBar* progress_ = nullptr;
  QAction *runAction_ = nullptr, *saveAction_ = nullptr,
          *saveAsAction_ = nullptr, *clearRoiAction_ = nullptr;
  QAction* singleRunAction_ = nullptr;
  QAction *currentRunAction_ = nullptr, *autoPreviewAction_ = nullptr;
  QMenu* recentProjectsMenu_ = nullptr;
  QAction *selectAction_ = nullptr, *panAction_ = nullptr,
          *rectangleAction_ = nullptr, *circleAction_ = nullptr,
          *overlayAction_ = nullptr, *cancelAction_ = nullptr;
};
}  // namespace workbench
