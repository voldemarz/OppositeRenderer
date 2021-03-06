/* 
 * Copyright (c) 2014 Opposite Renderer
 * For the full copyright and license information, please view the LICENSE.txt
 * file that was distributed with this source code.
 *
 * Contributions: Stian Pedersen
 *                Valdis Vilcans
*/

/*
Handles the OpenGL context.
Handles outputting the image to the screen including gamma correction using a OpenGL shader
Takes mouse events and delegate them to the Mouse class
*/

#define GL_GLEXT_PROTOTYPES
#include "config.h"
#include "GL/glew.h"
#include "RenderWidget.hxx"
#include <QTimer>
#include "renderer/Camera.h"
#include <QThread>
#include <QMutex>
#include <QMouseEvent>
#include "RenderWidget.hxx"
#include "models/OutputSettingsModel.hxx"
#include <QMessageBox>
#include <QLabel>
#include <fstream>

RenderWidget::RenderWidget( QWidget *parent, Camera & camera, const OutputSettingsModel & outputSettings ) : 
    QGLWidget(parent),
    m_camera(camera),
    m_mouse(Mouse(camera, outputSettings.getWidth(), outputSettings.getHeight())),
    m_outputSettingsModel(outputSettings),
    m_hasLoadedGLShaders(false),
    m_GLProgram(0),
    m_GLTextureSampler(0)
{
    this->resize(outputSettings.getWidth(), outputSettings.getHeight());
    setMouseTracking(false);
    m_displayBufferCpu = new float[MAX_OUTPUT_X*MAX_OUTPUT_Y*3];
    memset(m_displayBufferCpu, 0.f, MAX_OUTPUT_X*MAX_OUTPUT_Y*3*sizeof(float));

    m_iterationNumberLabel = new QLabel(this);
    m_iterationNumberLabel->setStyleSheet("background:rgb(51,51,51); font-size:20pt; color:rgb(170,170,170);");
    m_iterationNumberLabel->setAlignment(Qt::AlignRight);
    m_iterationNumberLabel->hide();
}

RenderWidget::~RenderWidget()
{
    delete m_displayBufferCpu;
    m_displayBufferCpu = NULL;
}

void RenderWidget::initializeGL()
{
    glewInit();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, -1, 1 );
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.2f, 0.2f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    if(!m_hasLoadedGLShaders)
    {
        initializeOpenGLShaders();
    }
}

void RenderWidget::displayFrame(const float *cpuBuffer, const unsigned long long const *lastRendererIterationNumber, 
                                QMutex * outputBufferMutex)
{
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw the resulting image
    assert(cpuBuffer);

    if (outputBufferMutex) (*outputBufferMutex).lock();
    m_previewedIterations = *lastRendererIterationNumber + 1; // iteration numbers 0-based
    memcpy(m_displayBufferCpu, cpuBuffer, getDisplayBufferSizeBytes());
    if (outputBufferMutex) (*outputBufferMutex).unlock();

    int offsetX = ((int)size().width() - (int)m_outputSettingsModel.getWidth())/2;
    int offsetY = ((int)size().height() - (int)m_outputSettingsModel.getHeight())/2;

    if(offsetY > 20)
    {
        m_iterationNumberLabel->show();
        m_iterationNumberLabel->setText(QString::number(*lastRendererIterationNumber));
        m_iterationNumberLabel->setGeometry(offsetX+m_outputSettingsModel.getWidth() - 250,
                                            offsetY+m_outputSettingsModel.getHeight() + 5, 250, 30);
    }
    else
    {
        m_iterationNumberLabel->hide();
    }

    glViewport(offsetX, offsetY, (GLint)m_outputSettingsModel.getWidth(), (GLint)m_outputSettingsModel.getHeight());

    glBindTexture(GL_TEXTURE_2D, m_GLOutputBufferTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, m_outputSettingsModel.getWidth(), 
        m_outputSettingsModel.getHeight(), 0, GL_RGB, GL_FLOAT, (GLvoid*)m_displayBufferCpu);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLint loc = glGetUniformLocation(m_GLProgram, "invgamma");
    if (loc != -1)
    {
        glUniform1f(loc, 1.0/m_outputSettingsModel.getGamma());
    }

    loc = glGetUniformLocation(m_GLProgram, "invIterations");
    if (loc != -1)
    {
        glUniform1f(loc, 1.0f/m_previewedIterations);
    }

    glEnable(GL_TEXTURE_2D);

    glBegin(GL_QUADS);

    glTexCoord2f(0, 0);
    glVertex2f(0, 0);

    glTexCoord2f(1, 0);
    glVertex2f(1, 0);

    glTexCoord2f(1, 1);
    glVertex2f(1, 1);
    
    glTexCoord2f(0, 1);
    glVertex2f(0, 1);

    glEnd();

    glDisable(GL_TEXTURE_2D);
}

void RenderWidget::onNewFrameReadyForDisplay(const float* cpuBuffer, const unsigned long long *lastRendererIterationNumber,
                                             QMutex * outputBufMutex)
{
    displayFrame(cpuBuffer, lastRendererIterationNumber, outputBufMutex);
    updateGL();
}

void RenderWidget::resizeGL( int w, int h )
{
    glClear(GL_COLOR_BUFFER_BIT);
}

void RenderWidget::paintGL()
{

}

size_t RenderWidget::getDisplayBufferSizeBytes()
{
    int width = m_outputSettingsModel.getWidth();
    int height = m_outputSettingsModel.getHeight();
    return width*height*3*sizeof(float);
}

void RenderWidget::mousePressEvent( QMouseEvent* event )
{
    m_mouse.handleMouseFunc( event->button(), 1, event->x(), event->y(), event->modifiers());
    emit cameraUpdated();
}

void RenderWidget::mouseMoveEvent( QMouseEvent* event )
{
    m_mouse.handleMoveFunc( event->x(), event->y());
    emit cameraUpdated();
}

// vmarz: FIXME called after all closable docks closed, need to repaint the viewport
void RenderWidget::resizeEvent( QResizeEvent* event )
{
    m_mouse.handleResize(event->size().width(), event->size().height());
    emit cameraUpdated();
}

void RenderWidget::initializeOpenGLShaders()
{
    m_hasLoadedGLShaders = true;

    const char* shaderSource = "uniform sampler2D sceneBuffer; "
                               "uniform float invgamma;"
                               "uniform float invIterations;"
                               "void main(){ "
                               "    vec2 uv = gl_TexCoord[0].xy;"
                               "    vec3 color = texture2D(sceneBuffer, uv).rgb;"
                               "    gl_FragColor.rgb = pow(color * invIterations, vec3(invgamma));"
                               "    gl_FragColor.a = 1.0;"
                               "}";

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSourceARB(fragmentShader, 1, &shaderSource, NULL);

    // Compile The Shader

    glCompileShaderARB(fragmentShader);
    GLint validCompilation;

    glGetObjectParameterivARB(fragmentShader, GL_COMPILE_STATUS, &validCompilation);
    if(!validCompilation)
    {
        GLint blen = 0;    
        GLsizei slen = 0;

        glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH , &blen);       
        if (blen > 1)
        {
            GLchar compiler_log[200];
            glGetInfoLogARB(fragmentShader, 199, &slen, compiler_log);
            printf("compiler_log:%s\n", compiler_log);
            QMessageBox::warning(this, "Error compiling OpenGL shader!", QString(compiler_log));
            exit(0);
        }
    }

    m_GLProgram = glCreateProgram();
    glAttachShader(m_GLProgram, fragmentShader);
    glLinkProgram(m_GLProgram); 
    glUseProgram(m_GLProgram);

    glGenSamplers(1, &m_GLTextureSampler);
    glSamplerParameteri(m_GLTextureSampler, GL_TEXTURE_WRAP_S, GL_REPEAT);

    glGenTextures(1, &m_GLOutputBufferTexture);
    glBindSampler(m_GLOutputBufferTexture, m_GLTextureSampler);

}


//////////////////////////////////////////////////////////////////////////
// Saving BMP
struct BmpHeader
{
    uint   mFileSize;        // Size of file in bytes
    uint   mReserved01;      // 2x 2 reserved bytes
    uint   mDataOffset;      // Offset in bytes where data can be found (54)

    uint   mHeaderSize;      // 40B
    int    mWidth;           // Width in pixels
    int    mHeight;          // Height in pixels

    short  mColorPlates;     // Must be 1
    short  mBitsPerPixel;    // We use 24bpp
    uint   mCompression;     // We use BI_RGB ~ 0, uncompressed
    uint   mImageSize;       // mWidth x mHeight x 3B
    uint   mHorizRes;        // Pixels per meter (75dpi ~ 2953ppm)
    uint   mVertRes;         // Pixels per meter (75dpi ~ 2953ppm)
    uint   mPaletteColors;   // Not using palette - 0
    uint   mImportantColors; // 0 - all are important
};

struct Vec3f
{
    float x,y,z;
};

void RenderWidget::saveImageAsBMP(const char * fileName)
{
    assert(m_displayBufferCpu);
    std::ofstream bmp(fileName, std::ios::binary);

    uint width = m_outputSettingsModel.getWidth();
    uint height = m_outputSettingsModel.getHeight();

    BmpHeader header;
    bmp.write("BM", 2);
    header.mFileSize   = uint(sizeof(BmpHeader) + 2) + width * height * 3;
    header.mReserved01 = 0;
    header.mDataOffset = uint(sizeof(BmpHeader) + 2);
    header.mHeaderSize = 40;
    header.mWidth      = width;
    header.mHeight     = height;
    header.mColorPlates     = 1;
    header.mBitsPerPixel    = 24;
    header.mCompression     = 0;
    header.mImageSize       = width * height * 3;
    header.mHorizRes        = 2953;
    header.mVertRes         = 2953;
    header.mPaletteColors   = 0;
    header.mImportantColors = 0;

    bmp.write((char*)&header, sizeof(header));
    Vec3f * img = reinterpret_cast<Vec3f*>(m_displayBufferCpu);

    const float invGamma = 1.f / m_outputSettingsModel.getGamma();
    const float invIterations = 1.f / m_previewedIterations;
    for(int y=0; y<height; y++)
    {
        for(int x=0; x<width; x++)
        {
            // bmp is stored from bottom up
            const Vec3f &rgbF = img[x + y*width];
            typedef unsigned char byte;
            float gammaBgr[3];
            gammaBgr[0] = std::pow(rgbF.z * invIterations, invGamma) * 255.f;
            gammaBgr[1] = std::pow(rgbF.y * invIterations, invGamma) * 255.f;
            gammaBgr[2] = std::pow(rgbF.x * invIterations, invGamma) * 255.f;

            byte bgrB[3];
            bgrB[0] = byte(std::min(255.f, std::max(0.f, gammaBgr[0])));
            bgrB[1] = byte(std::min(255.f, std::max(0.f, gammaBgr[1])));
            bgrB[2] = byte(std::min(255.f, std::max(0.f, gammaBgr[2])));

            bmp.write((char*)&bgrB, sizeof(bgrB));
        }
    }
}