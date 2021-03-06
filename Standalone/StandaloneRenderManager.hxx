/* 
 * Copyright (c) 2014 Opposite Renderer
 * For the full copyright and license information, please view the LICENSE.txt
 * file that was distributed with this source code.
 *
 * Contributions: Stian Pedersen
 *                Valdis Vilcans
*/

#include <QObject>
#include <QTime>
#include <QMutex>
#include "RunningStatus.h"
#include <optixu/optixpp_namespace.h>
#include "renderer/OptixRenderer.h"
#include "renderer/Camera.h"

class IScene;
class Application;
class QApplication;
class ComputeDevice;

class StandaloneRenderManager : public QObject
{
    Q_OBJECT;
public:
    StandaloneRenderManager(QApplication & qApplication, Application & application, const ComputeDevice& device);
    virtual ~StandaloneRenderManager();
    void renderNextIteration();
    void wait();

public slots:
    void start();

signals:
    void newFrameReadyForDisplay(const float* cpuBuffer, const unsigned long long *iterationNumber, QMutex * outputBufferMutex = NULL);
    void continueRayTracing();
    void renderManagerError(QString);

private slots:
    void onSceneLoadingNew();
    void onSceneUpdated();
    void onContinueRayTracing();
    void onSequenceNumberIncremented();
    void onRunningStatusChanged();

private:
    void fillRenderStatistics();
    void continueRayTracingIfRunningAsync();

    Application         & m_application;
    unsigned long long    m_nextIterationNumber;
    unsigned long long    m_lastRendererIterationNumber;

    OptixRenderer         m_renderer;
    Camera                m_camera;
    QTime                 renderTime;
    float               * m_outputBuffer;
    QMutex                m_outputBufferMutex;
    IScene              * m_currentScene;
    const ComputeDevice & m_device;
    double                m_PPMRadius;
    bool                  m_compileScene;
    bool                  m_noEmittedSignals;
};
