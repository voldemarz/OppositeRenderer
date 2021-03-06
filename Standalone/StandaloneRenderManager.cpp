/* 
 * Copyright (c) 2014 Opposite Renderer
 * For the full copyright and license information, please view the LICENSE.txt
 * file that was distributed with this source code.
 *
 * Contributions: Stian Pedersen
 *                Valdis Vilcans
*/

#include "config.h"
#include "StandaloneRenderManager.hxx"
#include "renderer/OptixRenderer.h"
#include <QThread>
#include "renderer/Camera.h"
#include <QTime>
#include "scene/Scene.h"
#include "scene/Cornell.h"
#include "clientserver/RenderServerRenderRequest.h"
#include <QCoreApplication>
#include <QApplication>
#include "Application.hxx"

StandaloneRenderManager::StandaloneRenderManager(QApplication & qApplication, Application & application, const ComputeDevice& device) :
    m_device(device),
    m_renderer(OptixRenderer()), 
    m_outputBuffer(NULL),
    m_nextIterationNumber(0),
    m_lastRendererIterationNumber(0),
    m_currentScene(NULL),
    m_compileScene(false),
    m_application(application),
    m_noEmittedSignals(true)
{
    connect(&application, SIGNAL(sequenceNumberIncremented()), this, SLOT(onSequenceNumberIncremented()));
    connect(&application, SIGNAL(runningStatusChanged()), this, SLOT(onRunningStatusChanged()));
    connect(&application.getSceneManager(), SIGNAL(sceneLoadingNew()), this, SLOT(onSceneLoadingNew()));
    connect(&application.getSceneManager(), SIGNAL(sceneUpdated()), this, SLOT(onSceneUpdated()));

    onSceneUpdated();

    connect(this, SIGNAL(continueRayTracing()), 
            this, SLOT(onContinueRayTracing()), 
            Qt::QueuedConnection);
}

StandaloneRenderManager::~StandaloneRenderManager()
{
    if(m_outputBuffer != NULL)
    {
        delete[] m_outputBuffer;
        m_outputBuffer = NULL;
    }
}

void StandaloneRenderManager::start()
{
    m_application.setRendererStatus(RendererStatus::INITIALIZING_ENGINE);
    try
    {
        m_renderer.initialize(m_device);
    }
    catch(const std::exception & E)
    {
        QString error = QString("Error during initialization: %1").arg(E.what());
        emit renderManagerError(error);
    }
}

void StandaloneRenderManager::onContinueRayTracing()
{
    renderNextIteration();
    continueRayTracingIfRunningAsync();
}

void StandaloneRenderManager::renderNextIteration()
{
    try
    {
        if(m_application.getRunningStatus() == RunningStatus::RUNNING && m_currentScene != NULL)
        {
            m_noEmittedSignals = true;

            if(m_compileScene)
            {
                m_application.setRendererStatus(RendererStatus::INITIALIZING_SCENE);
                m_renderer.initScene(*m_currentScene);
                m_compileScene = false;
                m_application.setRendererStatus(RendererStatus::STARTING_RENDERING);
            }

            // We only display one every X frames on screen (to make fair comparison with distributed renderer)
            bool shouldOutputIteration = m_nextIterationNumber % 5 == 0;
            //bool shouldOutputIteration = m_nextIterationNumber % 1 == 0;

            const double PPMAlpha = 2.0/3.0;
            QVector<unsigned long long> iterationNumbers;
            QVector<double> ppmRadii;

            RenderServerRenderRequestDetails details (m_camera, QByteArray(m_currentScene->getSceneName()), 
                m_application.getRenderMethod(), m_application.getWidth(), m_application.getHeight(), PPMAlpha);

            RenderServerRenderRequest renderRequest (m_application.getSequenceNumber(), iterationNumbers, ppmRadii, details);

            m_renderer.renderNextIteration(m_nextIterationNumber, m_nextIterationNumber, m_PPMRadius, shouldOutputIteration, renderRequest.getDetails());
            const double ppmRadiusSquared = m_PPMRadius*m_PPMRadius;
            const double ppmRadiusSquaredNew = ppmRadiusSquared*(m_nextIterationNumber+PPMAlpha)/double(m_nextIterationNumber+1);
            m_PPMRadius = sqrt(ppmRadiusSquaredNew);

            // set as rendering only after first iteration, since that one can slow due init and sync with gpu
            if (m_application.getRendererStatus() != RendererStatus::RENDERING)
                m_application.setRendererStatus(RendererStatus::RENDERING);

            // Transfer the output buffer to CPU and signal ready for display
            m_outputBufferMutex.lock();
            m_lastRendererIterationNumber = m_nextIterationNumber;
            if(shouldOutputIteration)
            {
                if(m_outputBuffer == NULL)
                {
                    m_outputBuffer = new float[MAX_OUTPUT_X*MAX_OUTPUT_Y*3];
                }
                m_renderer.getOutputBuffer(m_outputBuffer);
                // FIXME
                // vmarz: m_lastRendererIterationNumber shouldn't be exposed like that, but passed next iteration number
                // can be invalid (already incremented) when render widget is updating and accumulated values get scaled incorrectly
                emit newFrameReadyForDisplay(m_outputBuffer, &m_lastRendererIterationNumber, &m_outputBufferMutex);
            }
            m_outputBufferMutex.unlock();

            fillRenderStatistics();
            m_nextIterationNumber++;
        }
    }
    catch(const std::exception & E)
    {
        m_application.setRunningStatus(RunningStatus::PAUSE);
        QString error = QString("%1").arg(E.what());
        emit renderManagerError(error);
    }
}

/*
unsigned long long StandaloneRenderManager::getIterationNumber() const
{
    return m_nextIterationNumber-1;
}*/

void StandaloneRenderManager::fillRenderStatistics()
{
    m_application.getRenderStatisticsModel().setNumIterations(m_nextIterationNumber);
    m_application.getRenderStatisticsModel().setCurrentPPMRadius(m_PPMRadius);

    if(m_application.getRenderMethod() == RenderMethod::PROGRESSIVE_PHOTON_MAPPING)
    {
        m_application.getRenderStatisticsModel().setNumEmittedPhotonsPerIteration(OptixRenderer::EMITTED_PHOTONS_PER_ITERATION);
        m_application.getRenderStatisticsModel().setNumEmittedPhotons(OptixRenderer::EMITTED_PHOTONS_PER_ITERATION*m_nextIterationNumber);
    }
    else
    {
        m_application.getRenderStatisticsModel().setNumEmittedPhotonsPerIteration(0);
        m_application.getRenderStatisticsModel().setNumEmittedPhotons(0);
    }
}


void StandaloneRenderManager::onSequenceNumberIncremented()
{
    m_nextIterationNumber = 0;
    m_PPMRadius = m_application.getPPMSettingsModel().getPPMInitialRadius();
    m_camera = m_application.getCamera();
    continueRayTracingIfRunningAsync();
}

void StandaloneRenderManager::onSceneLoadingNew()
{
    //m_currentScene = NULL;
}

void StandaloneRenderManager::onSceneUpdated()
{
    IScene* scene = m_application.getSceneManager().getScene();
    if(scene != m_currentScene)
    {
        m_compileScene = true;
        m_currentScene = scene;
    }
}

void StandaloneRenderManager::wait()
{
    printf("StandaloneRenderManager::wait\n");
    /*m_thread->exit();
    m_thread->wait();*/
}

void StandaloneRenderManager::continueRayTracingIfRunningAsync()
{
    if(m_application.getRunningStatus() == RunningStatus::RUNNING && m_noEmittedSignals)
    {
        m_noEmittedSignals = false;
        emit continueRayTracing();
    }
}

void StandaloneRenderManager::onRunningStatusChanged()
{
    continueRayTracingIfRunningAsync();
}