/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kwindeformeffectprivate_p.h"
#include "kwingltexture.h"
#include "kwinglutils.h"

namespace KWin
{

GLTexture *DeformEffectPrivate::maybeRender(EffectWindow *window, DeformOffscreenData *offscreenData)
{
    const QRect geometry = window->expandedGeometry();
    QSize textureSize = offscreenData->textureSize.isEmpty() ? geometry.size() : offscreenData->textureSize;
    qWarning() << textureSize << geometry;
    if (const EffectScreen *screen = window->screen()) {
        textureSize *= screen->devicePixelRatio();
    }

    if (!offscreenData->texture || offscreenData->texture->size() != textureSize) {
        offscreenData->texture.reset(new GLTexture(GL_RGBA8, textureSize));
        offscreenData->texture->setFilter(GL_LINEAR);
        offscreenData->texture->setWrapMode(GL_CLAMP_TO_EDGE);
        offscreenData->renderTarget.reset(new GLRenderTarget(offscreenData->texture.data()));
        offscreenData->isDirty = true;
    }

    if (offscreenData->isDirty) {
        GLRenderTarget::pushRenderTarget(offscreenData->renderTarget.data());
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);

        QMatrix4x4 projectionMatrix;
        projectionMatrix.ortho(QRect(0, 0, textureSize.width(), textureSize.height()));

        WindowPaintData data(window);
        data.setXTranslation(-geometry.x());
        data.setYTranslation(-geometry.y());
        // data.setXScale(qreal(geometry.width())/textureSize.width());
        data.setOpacity(1.0);
        data.setProjectionMatrix(projectionMatrix);

        const int mask = Effect::PAINT_WINDOW_TRANSFORMED | Effect::PAINT_WINDOW_TRANSLUCENT;
        effects->drawWindow(window, mask, infiniteRegion(), data);

        GLRenderTarget::popRenderTarget();
        offscreenData->isDirty = false;
    }

    return offscreenData->texture.data();
}

void DeformEffectPrivate::paint(EffectWindow *window, GLTexture *texture, const QRegion &region,
                                const WindowPaintData &data, const WindowQuadList &quads)
{
    ShaderBinder binder(ShaderTrait::MapTexture | ShaderTrait::Modulate | ShaderTrait::AdjustSaturation);
    GLShader *shader = binder.shader();

    const bool indexedQuads = GLVertexBuffer::supportsIndexedQuads();
    const GLenum primitiveType = indexedQuads ? GL_QUADS : GL_TRIANGLES;
    const int verticesPerQuad = indexedQuads ? 4 : 6;

    const GLVertexAttrib attribs[] = {
        {VA_Position, 2, GL_FLOAT, offsetof(GLVertex2D, position)},
        {VA_TexCoord, 2, GL_FLOAT, offsetof(GLVertex2D, texcoord)},
    };

    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(attribs, 2, sizeof(GLVertex2D));
    const size_t size = verticesPerQuad * quads.count() * sizeof(GLVertex2D);
    GLVertex2D *map = static_cast<GLVertex2D *>(vbo->map(size));

    quads.makeInterleavedArrays(primitiveType, map, texture->matrix(NormalizedCoordinates));
    vbo->unmap();
    vbo->bindArrays();
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    const qreal rgb = data.brightness() * data.opacity();
    const qreal a = data.opacity();

    QMatrix4x4 mvp = data.screenProjectionMatrix();
    mvp.translate(window->x() + data.xTranslation(), window->y() + data.yTranslation());
    mvp.scale(data.xScale(), data.yScale());
    // mvp.scale(qreal(window->expandedGeometry().width()) / qreal(texture->width()) * data.xScale(),  qreal(window->expandedGeometry().height()) / qreal(texture->height()) * data.yScale());
    //qWarning()<<"AA"<< qreal(window->expandedGeometry().height()) / qreal(texture->height()) << 1 / data.yScale()<< data.yScale()<<qreal(window->expandedGeometry().height()) / qreal(texture->height()) * data.yScale();

    shader->setUniform(GLShader::ModelViewProjectionMatrix, mvp);
    shader->setUniform(GLShader::ModulationConstant, QVector4D(rgb, rgb, rgb, a));
    shader->setUniform(GLShader::Saturation, data.saturation());

    texture->bind();
    vbo->draw(effects->mapToRenderTarget(region), primitiveType, 0, verticesPerQuad * quads.count(), true);
    texture->unbind();

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    vbo->unbindArrays();
}

} // namespace KWin
