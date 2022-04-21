/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2021 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QPoint>
#include <QSharedPointer>
#include <QSize>
#include <QVector>

#include <chrono>
#include <xf86drmMode.h>

#include "colors.h"
#include "drm_object_plane.h"
#include "output.h"
#include "renderloop_p.h"

namespace KWin
{

class DrmGpu;
class DrmConnector;
class DrmCrtc;
class DrmBuffer;
class DrmDumbBuffer;
class GammaRamp;
class DrmConnectorMode;
class DrmPipelineLayer;

class DrmGammaRamp
{
public:
    DrmGammaRamp(DrmCrtc *crtc, const QSharedPointer<ColorTransformation> &transformation);
    ~DrmGammaRamp();

    const ColorLUT &lut() const;
    uint32_t blobId() const;

private:
    DrmGpu *m_gpu;
    const ColorLUT m_lut;
    uint32_t m_blobId = 0;
};

class DrmPipeline
{
public:
    DrmPipeline(DrmConnector *conn);
    ~DrmPipeline();

    /**
     * tests the pending commit first and commits it if the test passes
     * if the test fails, there is a guarantee for no lasting changes
     */
    bool present();
    bool testScanout();
    bool maybeModeset();

    bool needsModeset() const;
    void applyPendingChanges();
    void revertPendingChanges();

    bool setCursor(const QSharedPointer<DrmDumbBuffer> &buffer, const QPoint &hotspot = QPoint());
    bool moveCursor(QPoint pos);

    DrmConnector *connector() const;
    DrmCrtc *currentCrtc() const;
    DrmGpu *gpu() const;

    void pageFlipped(std::chrono::nanoseconds timestamp);
    bool pageflipPending() const;
    bool modesetPresentPending() const;
    void resetModesetPresentPending();
    void printDebugInfo() const;
    /**
     * what size buffers submitted to this pipeline should have
     */
    QSize bufferSize() const;

    QMap<uint32_t, QVector<uint64_t>> formats() const;

    void setOutput(DrmOutput *output);
    DrmOutput *output() const;

    struct State
    {
        DrmCrtc *crtc = nullptr;
        bool active = true; // whether or not the pipeline should be currently used
        bool enabled = true; // whether or not the pipeline needs a crtc
        QSharedPointer<DrmConnectorMode> mode;
        uint32_t overscan = 0;
        Output::RgbRange rgbRange = Output::RgbRange::Automatic;
        RenderLoopPrivate::SyncMode syncMode = RenderLoopPrivate::SyncMode::Fixed;
        QSharedPointer<ColorTransformation> colorTransformation;
        QSharedPointer<DrmGammaRamp> gamma;

        QSharedPointer<DrmPipelineLayer> layer;

        QPoint cursorPos;
        QPoint cursorHotspot;
        QSharedPointer<DrmDumbBuffer> cursorBo;

        // the transformation that this pipeline will apply to submitted buffers
        DrmPlane::Transformations bufferOrientation = DrmPlane::Transformation::Rotate0;
        // the transformation that buffers submitted to the pipeline should have
        DrmPlane::Transformations renderOrientation = DrmPlane::Transformation::Rotate0;
    };
    State pending;

    enum class CommitMode {
        Test,
        Commit,
        CommitModeset
    };
    Q_ENUM(CommitMode);
    static bool commitPipelines(const QVector<DrmPipeline *> &pipelines, CommitMode mode, const QVector<DrmObject *> &unusedObjects = {});

private:
    bool activePending() const;
    bool isCursorVisible() const;
    bool isBufferForDirectScanout() const;
    uint32_t calculateUnderscan();

    // legacy only
    bool presentLegacy();
    bool legacyModeset();
    bool applyPendingChangesLegacy();
    bool setCursorLegacy();
    bool moveCursorLegacy();
    static bool commitPipelinesLegacy(const QVector<DrmPipeline *> &pipelines, CommitMode mode);

    // atomic modesetting only
    bool populateAtomicValues(drmModeAtomicReq *req, uint32_t &flags);
    void atomicCommitFailed();
    void atomicCommitSuccessful(CommitMode mode);
    bool prepareAtomicModeset();
    static bool commitPipelinesAtomic(const QVector<DrmPipeline *> &pipelines, CommitMode mode, const QVector<DrmObject *> &unusedObjects);

    // logging helpers
    enum class PrintMode {
        OnlyChanged,
        All,
    };
    static void printFlags(uint32_t flags);
    static void printProps(DrmObject *object, PrintMode mode);

    DrmOutput *m_output = nullptr;
    DrmConnector *m_connector = nullptr;

    bool m_pageflipPending = false;
    bool m_modesetPresentPending = false;

    // the state that will be applied at the next real atomic commit
    State m_next;
    // the state that is already committed
    State m_current;
};

}
