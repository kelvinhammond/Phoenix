

#include "audio.h"


Audio::Audio(QObject *parent)
            : QObject(parent),
              isRunning(false),
              aout(nullptr),
              aio(nullptr),
              timer(this)
{
        this->moveToThread(&thread);
        connect(&thread, SIGNAL(started()), SLOT(threadStarted()));
        thread.setObjectName("phoenix-audio");

        original_sample_rate = 0;
        deviation = 0.005;

        m_abuf = new AudioBuffer();
        Q_CHECK_PTR(m_abuf);
        //connect(m_abuf, SIGNAL(hasPeriodSize()), this, SLOT(handleHasPeriodSize()));

        timer.moveToThread(&thread);
        connect(&timer, SIGNAL(timeout()), this, SLOT(handlePeriodTimer()));

        // we need send this signal to ourselves
        connect(this, SIGNAL(formatChanged()), this, SLOT(handleFormatChanged()));
}

void Audio::start()
{
    thread.start(QThread::HighestPriority);
}

/* This needs to be called on the audio thread*/
void Audio::setFormat(QAudioFormat _afmt)
{
    qCDebug(phxAudio, "setFormat(%iHz %ibits)", _afmt.sampleRate(), _afmt.sampleSize());
/*    QAudioDeviceInfo info(QAudioDeviceInfo::defaultOutputDevice());
    if (!info.isFormatSupported(_afmt)) {
        qCWarning(phxAudio) << "Audio format not supported by output device !";
        return;
    }*/
    afmt = _afmt;
    emit formatChanged();
}

void Audio::handleFormatChanged()
{
    if (aout) {
        aout->stop();
        delete aout;
    }
    aout = new QAudioOutput(afmt);
    Q_CHECK_PTR(aout);
    aout->moveToThread(&thread);

    connect(aout, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
    aio = aout->start();
    if (!isRunning)
        aout->suspend();

    timer.setInterval(afmt.durationForBytes(aout->periodSize() * 1.5) / 1000);
    aio->moveToThread(&thread);
}

void Audio::threadStarted()
{
    if(!afmt.isValid()) {
        // we don't have a valid audio format yet...
        qCDebug(phxAudio) << "afmt is not valid";
        return;
    }
    handleFormatChanged();
}

void Audio::handlePeriodTimer()
{
    Q_ASSERT(QThread::currentThread() == &thread);
    if (!aio) {
        static bool error_msg = true;
        if (error_msg) {
            qCDebug(phxAudio) << "Audio device was not found, stopping all audio writes.";
            error_msg= false;
        }
        return;
    }
    int toWrite = aout->bytesFree();
    if(!toWrite)
        return;

    if (!original_sample_rate)
        original_sample_rate = afmt.sampleRate();

    int half_size = aout->bufferSize() / 2;
    int delta_mid = toWrite - half_size;
    qreal direction = (qreal)delta_mid / half_size;
    qreal adjust = 1.0 + deviation * direction;

    afmt.setSampleRate(original_sample_rate * adjust);

    qCDebug(phxAudio) << afmt.sampleRate();

    QVarLengthArray<char, 4096*4> tmpbuf(toWrite);
    int read = m_abuf->read(tmpbuf.data(), toWrite);
    int wrote = aio->write(tmpbuf.data(), read);
    Q_UNUSED(wrote);
}

void Audio::runChanged(bool _isRunning)
{
    isRunning = _isRunning;
    if(!aout)
        return;
    if(!isRunning) {
        if(aout->state() != QAudio::SuspendedState) {
            qCDebug(phxAudio) << "Paused";
            aout->suspend();
            timer.stop();
        }
    } else {
        if(aout->state() != QAudio::ActiveState) {
            qCDebug(phxAudio) << "Started";
            aout->resume();
            timer.start();
        }
    }
}

void Audio::stateChanged(QAudio::State s)
{
    if(s == QAudio::IdleState && aout->error() == QAudio::UnderrunError) {
        aio = aout->start();
    }
    if(s != QAudio::IdleState && s != QAudio::ActiveState) {
        qCDebug(phxAudio) << "State changed:" << s;
    }
}

void Audio::setVolume(qreal level)
{
    if (aout)
        aout->setVolume(level);
}

Audio::~Audio()
{
    if(aout)
        delete aout;
    if(aio)
        delete aio;
    if(m_abuf)
        delete m_abuf;
}
