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

LightView::LightView()
{
    m_lightbuffer = g_framebuffers.createFrameBuffer();
    m_lightTexture = generateLightBubble(.0f);
    m_blendEquation = Painter::BlendEquation_Add;
    reset();
}

TexturePtr LightView::generateLightBubble(float centerFactor)
{
    const int bubbleRadius = 256;
    const int centerRadius = bubbleRadius * centerFactor;
    const int bubbleDiameter = bubbleRadius * 2;
    ImagePtr lightImage = ImagePtr(new Image(Size(bubbleDiameter, bubbleDiameter)));

    for(int x = 0; x < bubbleDiameter; ++x) {
        for(int y = 0; y < bubbleDiameter; ++y) {
            const float radius = std::sqrt((bubbleRadius - x) * (bubbleRadius - x) + (bubbleRadius - y) * (bubbleRadius - y));
            float intensity = stdext::clamp<float>((bubbleRadius - radius) / static_cast<float>(bubbleRadius - centerRadius), 0.0f, 1.0f);

            // light intensity varies inversely with the square of the distance
            intensity *= intensity;
            const uint8_t colorByte = intensity * 0xB4;

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

void LightView::addLightSource(const Position& pos, const Point& center, float scaleFactor, const Light& light)
{
    const int intensity = light.intensity;
    const int radius = (Otc::TILE_PIXELS * scaleFactor) * 1.7;

    Color color = Color::from8bit(light.color);

    const float brightnessLevel = light.intensity > 1 ? 0.7 : 0.2f;
    const float brightness = brightnessLevel + (intensity / static_cast<float>(MAX_LIGHT_INTENSITY)) * brightnessLevel;

    color.setRed(color.rF() * brightness);
    color.setGreen(color.gF() * brightness);
    color.setBlue(color.bF() * brightness);

    /*if(m_blendEquation == Painter::BlendEquation_Add && !m_lightMap.empty()) {
        const LightSource prevSource = m_lightMap.back();
        if(prevSource.center == center && prevSource.color == color && prevSource.radius == radius)
            return;
    }*/

    const int s = std::floor(static_cast<float>(intensity) / 2);
    const int start = s * -1;
    int y = start;

    for(int x = start; x <= s; ++x) {
        for(int y = start; y <= s; ++y) {
            int absY = std::abs(y);
            int absX = std::abs(x);
            int diff = s / 2;
            int diff2 = diff + diff;

            if(absX == s && absY == 1 || absY == s && absX == 1) continue;
            if(absY > diff && absX > diff && (absY == absX || absX - diff == absY || absX == absY - diff || absX - diff == absY - diff)) continue;


            LightSource source2;
            source2.center = (center + ((Point(x, y) * Otc::TILE_PIXELS)));
            source2.color = color;
            source2.radius = radius;
            m_lightMap.push_back(source2);
        }
    }
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

void LightView::drawLightSource(const Point& center, const Color& color, int radius)
{
    // debug draw
    //radius /= 16;

    const Rect dest = Rect(center - Point(radius, radius), Size(radius * 2, radius * 2));
    g_painter->setColor(color);
    g_painter->drawTexturedRect(dest, m_lightTexture);
}

void LightView::resize(const Size& size)
{
    m_lightbuffer->resize(size);
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

        for(const LightSource& source : m_lightMap) {
            drawLightSource(source.center, source.color, source.radius);
        }

        m_lightMap.clear();

        m_lightbuffer->release();
    }
    g_painter->setCompositionMode(Painter::CompositionMode_Light);

    m_lightbuffer->draw(dest, src);
    g_painter->restoreSavedState();
}
