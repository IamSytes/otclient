/*
 * Copyright (c) 2010-2020 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "lightview.h"
#include <framework/graphics/framebuffer.h>
#include <framework/graphics/framebuffermanager.h>
#include <framework/graphics/image.h>
#include <framework/graphics/painter.h>
#include "mapview.h"
#include "map.h"

enum {
    MAX_LIGHT_INTENSITY = 8,
    MAX_AMBIENT_LIGHT_INTENSITY = 255
};

LightView::LightView(const MapViewPtr& mapView, const uint8 version)
{
    m_mapView = mapView;
    m_version = version;

    m_lightbuffer = g_framebuffers.createFrameBuffer();
    m_lightTexture = generateLightBubble();
    m_blendEquation = Painter::BlendEquation_Add;

    reset();
}

TexturePtr LightView::generateLightBubble()
{
    int intensityVariant;
    float centerFactor;
    if(m_version == 1) {
        centerFactor = .1f;
        intensityVariant = 0xB4;
    } else {
        centerFactor = .0f;
        intensityVariant = 0xff;
    }

    const int bubbleRadius = 256;
    const int centerRadius = bubbleRadius * centerFactor;
    const int bubbleDiameter = bubbleRadius * 2;
    ImagePtr lightImage = ImagePtr(new Image(Size(bubbleDiameter, bubbleDiameter)));

    for(int x = 0; x < bubbleDiameter; ++x) {
        for(int y = 0; y < bubbleDiameter; ++y) {
            const float radius = std::sqrt((bubbleRadius - x) * (bubbleRadius - x) + (bubbleRadius - y) * (bubbleRadius - y));
            float intensity = stdext::clamp<float>((bubbleRadius - radius) / static_cast<float>(bubbleRadius - centerRadius), .0f, 1.0f);
            // light intensity varies inversely with the square of the distance
            intensity *= intensity;
            if(m_version > 1) intensity = std::min<float>(intensity, 0.6);
            const uint8_t colorByte = intensity * intensityVariant;

            uint8_t pixel[4] = { colorByte, colorByte, colorByte, 0xff };
            lightImage->setPixel(x, y, pixel);
        }
    }

    TexturePtr tex = TexturePtr(new Texture(lightImage, true));
    tex->setSmooth(true);
    return tex;
}

void LightView::reset()
{
    m_lightMap.clear();
}

void LightView::setGlobalLight(const Light& light)
{
    m_globalLight = light;
}

void LightView::addLightSource(const Position& pos, const Point& center, float scaleFactor, const Light& light, const ThingPtr& thing)
{
    if(m_version == 1) {
        Point& walkOffset = Point();
        if(thing && thing->isCreature()) {
            const CreaturePtr& c = thing->static_self_cast<Creature>();
            walkOffset = c->getWalkOffset();
        }

        addLightSourceV1(center + (walkOffset + Point(16, 16)) * scaleFactor, scaleFactor, light);
    } else if(m_version == 2) {
        addLightSourceV2(pos, center, scaleFactor, light, thing);
    }
}

void LightView::addLightSourceV1(const Point& center, float scaleFactor, const Light& light)
{
    const int intensity = light.intensity;
    const int radius = (intensity * Otc::TILE_PIXELS * scaleFactor) * 2.5;

    Color color = Color::from8bit(light.color);

    const float brightnessLevel = intensity > 1 ? 0.7 : 0.2f;
    const float brightness = brightnessLevel + (intensity / static_cast<float>(MAX_LIGHT_INTENSITY)) * brightnessLevel;

    color.setRed(color.rF() * brightness);
    color.setGreen(color.gF() * brightness);
    color.setBlue(color.bF() * brightness);

    if(m_blendEquation == Painter::BlendEquation_Add && !m_lightMap.empty()) {
        const std::pair< LightSource, LightSource> prevSource = m_lightMap.back();
        if(prevSource.first.center == center && prevSource.first.color == color && prevSource.first.radius == radius)
            return;
    }

    LightSource source;
    source.center = center;
    source.color = color;
    source.radius = radius;
    m_lightMap.push_back(std::make_pair(source, LightSource()));
}

void LightView::addLightSourceV2(const Position& pos, const Point& center, float scaleFactor, const Light& light, const ThingPtr& thing)
{
    int intensity = light.intensity;
    if(light.intensity > MAX_LIGHT_INTENSITY) {
        const auto& awareRange = m_mapView->m_awareRange;
        intensity = std::max<int>(awareRange.right, awareRange.bottom) + 2;
    }

    const int radius = (Otc::TILE_PIXELS * scaleFactor) * 2.5;
    const float brightnessLevel = 0.1;
    const float brightness = brightnessLevel + (intensity / static_cast<float>(MAX_LIGHT_INTENSITY)) * brightnessLevel;
    const Position& posTile = pos.isValid() ? pos : m_mapView->getPosition(center, m_mapView->m_srcRect.size());
    const Point& cleanPoint = Point(16, 16) * scaleFactor;


    Point& moveOffset = Point();
    CreaturePtr creature;
    bool isMoving = false;
    if(thing && thing->isCreature()) {
        creature = thing->static_self_cast<Creature>();
        if(!creature->getWalkOffset().isNull()) isMoving = true;
        moveOffset = (creature->getWalkOffset() + Point(15, 15)) * scaleFactor;
    }

    Color color = Color::from8bit(light.color);
    color.setRed(color.rF() * brightness);
    color.setGreen(color.gF() * brightness);
    color.setBlue(color.bF() * brightness);

    const auto& dimensions = getDimensions(intensity);
    for each(const auto dimension in dimensions)
    {
        const auto x = dimension.first;
        const auto y = dimension.second;

        auto posLight = posTile.translated(x, y);
        int index = getLightSourceIndex(posLight);
        if(index == -1 || !canDraw(posLight)) {
            continue;
        }

        const auto& newCenter = center + ((Point(x, y) * Otc::TILE_PIXELS) * scaleFactor);

        LightSource light;
        light.center = newCenter + moveOffset;
        light.color = color;
        light.radius = radius;
        light.pos = posLight;
        light.intensity = intensity;

        auto& lightSource = m_lightMap[index];

        if(lightSource.first.hasLight()) {
            lightSource.second = light;
        } else {
            lightSource.first = light;
        }
    }
}

std::vector<std::pair<int8_t, int8_t>> LightView::getDimensions(const uint8 intensity)
{
    auto& dimension = m_dimensionCache[intensity];
    if(dimension.empty()) {
        const int size = std::floor(static_cast<float>(intensity) / 1.3),
            start = size * -1,
            middle = size / 2;

        for(int x = start; x <= size; ++x) {
            for(int y = start; y <= size; ++y) {
                const int absY = std::abs(y);
                const int absX = std::abs(x);

                if(absX == size && absY >= 1 || absY == size && absX >= 1) continue;
                if(absY > middle && absX > middle && (
                    absY == absX || absX - middle == absY || absX == absY - middle || absX - middle == absY - middle
                    ) || absX == size && absY == 0 || absY == size && absX == 0) continue;

                dimension.push_back(std::make_pair(x, y));
            }
        }
    }

    return dimension;
}

int LightView::getLightSourceIndex(const Position& pos)
{
    const auto& point = m_mapView->transformPositionTo2D(pos, m_mapView->getCameraPosition());
    size_t index = (m_mapView->m_drawDimension.width() * (point.y / Otc::TILE_PIXELS)) + (point.x / Otc::TILE_PIXELS);

    if(index >= m_lightMap.size()) return -1;

    return index;
}

bool LightView::canDraw(const Position& pos)
{
    TilePtr tile = g_map.getTile(pos);
    if(!tile || tile->isCovered() || tile->isTopGround() && !tile->hasBottomToDraw() || tile->isBlockLight()) {
        return false;
    }

    if(pos.z != m_mapView->getCachedFirstVisibleFloor()) {
        tile = g_map.getTile(pos.translated(1, 1, -1));
        if(tile && tile->isBlockLight()) return false;
    }

    return true;
}

void LightView::drawGlobalLight(const Light& light)
{
    Color color = Color::from8bit(light.color);
    const float brightness = light.intensity / static_cast<float>(MAX_AMBIENT_LIGHT_INTENSITY);
    color.setRed(color.rF() * brightness);
    color.setGreen(color.gF() * brightness);
    color.setBlue(color.bF() * brightness);

    g_painter->setColor(color);
    g_painter->drawFilledRect(Rect(0, 0, m_lightbuffer->getSize()));
}

void LightView::drawLightSource(const LightSource& light)
{
    // debug draw
    //radius /= 16;

    const Rect dest = Rect(light.center - Point(light.radius, light.radius), Size(light.radius * 2, light.radius * 2));
    g_painter->setColor(light.color);
    g_painter->drawTexturedRect(dest, m_lightTexture);
}

void LightView::resize()
{
    m_lightbuffer->resize(m_mapView->m_frameCache.tile->getSize());

    if(m_version == 2) {
        m_lightMap.resize(m_mapView->m_drawDimension.area());
    }
}

void LightView::draw(const Rect& dest, const Rect& src)
{
    // draw light, only if there is darkness
    if(!isDark() || m_lightbuffer->getTexture() == nullptr) return;

    g_painter->saveAndResetState();
    if(m_lightbuffer->canUpdate()) {
        m_lightbuffer->bind();
        g_painter->setCompositionMode(Painter::CompositionMode_Replace);

        drawGlobalLight(m_globalLight);

        g_painter->setBlendEquation(m_blendEquation);
        g_painter->setCompositionMode(Painter::CompositionMode_Add);

        if(m_version == 1) {
            for(const std::pair<LightSource, LightSource>& source : m_lightMap)
                drawLightSource(source.first);

            m_lightMap.clear();
        } else if(m_version == 2) {
            for(std::pair<LightSource, LightSource>& source : m_lightMap) {
                if(source.first.pos.isValid()) {
                    drawLightSource(source.first);
                    source.first.reset();
                }

                if(source.second.pos.isValid()) {
                    drawLightSource(source.second);
                    source.second.reset();
                }
            }
        }

        m_lightbuffer->release();
    }
    g_painter->setCompositionMode(Painter::CompositionMode_Light);

    m_lightbuffer->draw(dest, src);
    g_painter->restoreSavedState();
}
