/******************************************************************************
* Spine Runtimes Software License v2.5
*
* Copyright (c) 2013-2016, Esoteric Software
* All rights reserved.
*
* You are granted a perpetual, non-exclusive, non-sublicensable, and
* non-transferable license to use, install, execute, and perform the Spine
* Runtimes software and derivative works solely for personal or internal
* use. Without the written permission of Esoteric Software (see Section 2 of
* the Spine Software License Agreement), you may not (a) modify, translate,
* adapt, or develop new applications using the Spine Runtimes or otherwise
* create derivative works or improvements of the Spine Runtimes or (b) remove,
* delete, alter, or obscure any trademarks or any copyright, trademark, patent,
* or other intellectual property or proprietary rights notices on or in the
* Software, including any copy thereof. Redistributions in binary or source
* form must include this license and terms.
*
* THIS SOFTWARE IS PROVIDED BY ESOTERIC SOFTWARE "AS IS" AND ANY EXPRESS OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
* EVENT SHALL ESOTERIC SOFTWARE BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, BUSINESS INTERRUPTION, OR LOSS OF
* USE, DATA, OR PROFITS) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

#include <spine/PathConstraint.h>

#include <spine/PathConstraintData.h>
#include <spine/Skeleton.h>
#include <spine/PathAttachment.h>
#include <spine/Bone.h>
#include <spine/Slot.h>

#include <spine/SlotData.h>
#include <spine/BoneData.h>
#include <spine/SpacingMode.h>
#include <spine/PositionMode.h>
#include <spine/RotateMode.h>
#include <spine/MathUtil.h>

namespace Spine
{
    RTTI_IMPL(PathConstraint, Constraint);
    
    const float PathConstraint::EPSILON = 0.00001f;
    const int PathConstraint::NONE = -1;
    const int PathConstraint::BEFORE = -2;
    const int PathConstraint::AFTER = -3;
    
    PathConstraint::PathConstraint(PathConstraintData& data, Skeleton& skeleton) : Constraint(),
    _data(data),
    _target(skeleton.findSlot(data.getTarget()->getName())),
    _position(data.getPosition()),
    _spacing(data.getSpacing()),
    _rotateMix(data.getRotateMix()),
    _translateMix(data.getTranslateMix())
    {
        _bones.reserve(_data.getBones().size());
        for (BoneData** i = _data.getBones().begin(); i != _data.getBones().end(); ++i)
        {
            BoneData* boneData = (*i);
            
            _bones.push_back(skeleton.findBone(boneData->getName()));
        }
        
        _segments.reserve(10);
        _segments.setSize(10);
    }
    
    void PathConstraint::apply()
    {
        update();
    }
    
    void PathConstraint::update()
    {
        Attachment* baseAttachment = _target->getAttachment();
        if (baseAttachment == NULL || !baseAttachment->getRTTI().derivesFrom(PathAttachment::rtti))
        {
            return;
        }
        
        PathAttachment* attachment = static_cast<PathAttachment*>(baseAttachment);
        
        float rotateMix = _rotateMix;
        float translateMix = _translateMix;
        bool translate = translateMix > 0;
        bool rotate = rotateMix > 0;
        if (!translate && !rotate)
        {
            return;
        }
        
        PathConstraintData data = _data;
        SpacingMode spacingMode = data._spacingMode;
        bool lengthSpacing = spacingMode == SpacingMode_Length;
        RotateMode rotateMode = data._rotateMode;
        bool tangents = rotateMode == RotateMode_Tangent, scale = rotateMode == RotateMode_ChainScale;
        size_t boneCount = _bones.size();
        int spacesCount = static_cast<int>(tangents ? boneCount : boneCount + 1);
        _spaces.reserve(spacesCount);
        _spaces.setSize(spacesCount);
        float spacing = _spacing;
        if (scale || lengthSpacing)
        {
            if (scale)
            {
                _lengths.reserve(boneCount);
                _lengths.setSize(boneCount);
            }
            
            for (int i = 0, n = spacesCount - 1; i < n;)
            {
                Bone* boneP = _bones[i];
                Bone& bone = *boneP;
                float setupLength = bone._data.getLength();
                if (setupLength < PathConstraint::EPSILON)
                {
                    if (scale)
                    {
                        _lengths[i] = 0;
                    }
                    _spaces[++i] = 0;
                }
                else
                {
                    float x = setupLength * bone._a;
                    float y = setupLength * bone._c;
                    float length = (float)sqrt(x * x + y * y);
                    if (scale)
                    {
                        _lengths[i] = length;
                    }
                    
                    _spaces[++i] = (lengthSpacing ? setupLength + spacing : spacing) * length / setupLength;
                }
            }
        }
        else
        {
            for (int i = 1; i < spacesCount; ++i)
            {
                _spaces[i] = spacing;
            }
        }
        
        Vector<float> positions = computeWorldPositions(*attachment, spacesCount, tangents, data.getPositionMode() == PositionMode_Percent, spacingMode == SpacingMode_Percent);
        float boneX = positions[0];
        float boneY = positions[1];
        float offsetRotation = data.getOffsetRotation();
        bool tip;
        if (offsetRotation == 0)
        {
            tip = rotateMode == RotateMode_Chain;
        }
        else
        {
            tip = false;
            Bone p = _target->getBone();
            offsetRotation *= p.getA() * p.getD() - p.getB() * p.getC() > 0 ? DegRad : -DegRad;
        }
        
        for (int i = 0, p = 3; i < boneCount; i++, p += 3)
        {
            Bone* boneP = _bones[i];
            Bone& bone = *boneP;
            bone._worldX += (boneX - bone._worldX) * translateMix;
            bone._worldY += (boneY - bone._worldY) * translateMix;
            float x = positions[p];
            float y = positions[p + 1];
            float dx = x - boneX;
            float dy = y - boneY;
            if (scale)
            {
                float length = _lengths[i];
                if (length >= PathConstraint::EPSILON)
                {
                    float s = ((float)sqrt(dx * dx + dy * dy) / length - 1) * rotateMix + 1;
                    bone._a *= s;
                    bone._c *= s;
                }
            }
            
            boneX = x;
            boneY = y;
            
            if (rotate)
            {
                float a = bone._a, b = bone._b, c = bone._c, d = bone._d, r, cos, sin;
                if (tangents)
                {
                    r = positions[p - 1];
                }
                else if (_spaces[i + 1] < PathConstraint::EPSILON)
                {
                    r = positions[p + 2];
                }
                else
                {
                    r = MathUtil::atan2(dy, dx);
                }
                
                r -= MathUtil::atan2(c, a);
                
                if (tip)
                {
                    cos = MathUtil::cos(r);
                    sin = MathUtil::sin(r);
                    float length = bone._data.getLength();
                    boneX += (length * (cos * a - sin * c) - dx) * rotateMix;
                    boneY += (length * (sin * a + cos * c) - dy) * rotateMix;
                }
                else
                {
                    r += offsetRotation;
                }
                
                if (r > SPINE_PI)
                {
                    r -= SPINE_PI_2;
                }
                else if (r < -SPINE_PI)
                {
                    r += SPINE_PI_2;
                }
                
                r *= rotateMix;
                cos = MathUtil::cos(r);
                sin = MathUtil::sin(r);
                bone._a = cos * a - sin * c;
                bone._b = cos * b - sin * d;
                bone._c = sin * a + cos * c;
                bone._d = sin * b + cos * d;
            }
            
            bone._appliedValid = false;
        }
    }
    
    int PathConstraint::getOrder()
    {
        return _data.getOrder();
    }
    
    float PathConstraint::getPosition()
    {
        return _position;
    }
    
    void PathConstraint::setPosition(float inValue)
    {
        _position = inValue;
    }
    
    float PathConstraint::getSpacing()
    {
        return _spacing;
    }
    
    void PathConstraint::setSpacing(float inValue)
    {
        _spacing = inValue;
    }
    
    float PathConstraint::getRotateMix()
    {
        return _rotateMix;
    }
    
    void PathConstraint::setRotateMix(float inValue)
    {
        _rotateMix = inValue;
    }
    
    float PathConstraint::getTranslateMix()
    {
        return _translateMix;
    }
    
    void PathConstraint::setTranslateMix(float inValue)
    {
        _translateMix = inValue;
    }
    
    Vector<Bone*>& PathConstraint::getBones()
    {
        return _bones;
    }
    
    Slot* PathConstraint::getTarget()
    {
        return _target;
    }
    
    void PathConstraint::setTarget(Slot* inValue)
    {
        _target = inValue;
    }
    
    PathConstraintData& PathConstraint::getData()
    {
        return _data;
    }
    
    Vector<float> PathConstraint::computeWorldPositions(PathAttachment& path, int spacesCount, bool tangents, bool percentPosition, bool percentSpacing)
    {
        Slot& target = *_target;
        float position = _position;
        _positions.reserve(spacesCount * 3 + 2);
        _positions.setSize(spacesCount * 3 + 2);
        bool closed = path.isClosed();
        int verticesLength = path.getWorldVerticesLength();
        int curveCount = verticesLength / 6;
        int prevCurve = NONE;
        
        float pathLength;
        if (!path.isConstantSpeed())
        {
            Vector<float>& lengths = path.getLengths();
            curveCount -= closed ? 1 : 2;
            pathLength = lengths[curveCount];
            if (percentPosition)
            {
                position *= pathLength;
            }
            
            if (percentSpacing)
            {
                for (int i = 0; i < spacesCount; ++i)
                {
                    _spaces[i] *= pathLength;
                }
            }
            
            _world.reserve(8);
            _world.setSize(8);
            for (int i = 0, o = 0, curve = 0; i < spacesCount; i++, o += 3)
            {
                float space = _spaces[i];
                position += space;
                float p = position;
                
                if (closed)
                {
                    p = fmod(p, pathLength);
                    
                    if (p < 0)
                    {
                        p += pathLength;
                    }
                    curve = 0;
                }
                else if (p < 0)
                {
                    if (prevCurve != BEFORE)
                    {
                        prevCurve = BEFORE;
                        path.computeWorldVertices(target, 2, 4, _world, 0);
                    }
                    
                    addBeforePosition(p, _world, 0, _positions, o);
                    
                    continue;
                }
                else if (p > pathLength)
                {
                    if (prevCurve != AFTER)
                    {
                        prevCurve = AFTER;
                        path.computeWorldVertices(target, verticesLength - 6, 4, _world, 0);
                    }
                    
                    addAfterPosition(p - pathLength, _world, 0, _positions, o);
                    
                    continue;
                }
                
                // Determine curve containing position.
                for (;; curve++)
                {
                    float length = lengths[curve];
                    if (p > length)
                    {
                        continue;
                    }
                    
                    if (curve == 0)
                    {
                        p /= length;
                    }
                    else
                    {
                        float prev = lengths[curve - 1];
                        p = (p - prev) / (length - prev);
                    }
                    break;
                }
                
                if (curve != prevCurve)
                {
                    prevCurve = curve;
                    if (closed && curve == curveCount)
                    {
                        path.computeWorldVertices(target, verticesLength - 4, 4, _world, 0);
                        path.computeWorldVertices(target, 0, 4, _world, 4);
                    }
                    else
                    {
                        path.computeWorldVertices(target, curve * 6 + 2, 8, _world, 0);
                    }
                }
                
                addCurvePosition(p, _world[0], _world[1], _world[2], _world[3], _world[4], _world[5], _world[6], _world[7], _positions, o, tangents || (i > 0 && space < EPSILON));
            }
            return _world;
        }
        
        // World vertices.
        if (closed)
        {
            verticesLength += 2;
            _world.reserve(verticesLength);
            _world.setSize(verticesLength);
            path.computeWorldVertices(target, 2, verticesLength - 4, _world, 0);
            path.computeWorldVertices(target, 0, 2, _world, verticesLength - 4);
            _world[verticesLength - 2] = _world[0];
            _world[verticesLength - 1] = _world[1];
        }
        else
        {
            curveCount--;
            verticesLength -= 4;
            _world.reserve(verticesLength);
            _world.setSize(verticesLength);
            path.computeWorldVertices(target, 2, verticesLength, _world, 0);
        }
        
        // Curve lengths.
        _curves.reserve(curveCount);
        _curves.setSize(curveCount);
        pathLength = 0;
        float x1 = _world[0], y1 = _world[1], cx1 = 0, cy1 = 0, cx2 = 0, cy2 = 0, x2 = 0, y2 = 0;
        float tmpx, tmpy, dddfx, dddfy, ddfx, ddfy, dfx, dfy;
        for (int i = 0, w = 2; i < curveCount; i++, w += 6)
        {
            cx1 = _world[w];
            cy1 = _world[w + 1];
            cx2 = _world[w + 2];
            cy2 = _world[w + 3];
            x2 = _world[w + 4];
            y2 = _world[w + 5];
            tmpx = (x1 - cx1 * 2 + cx2) * 0.1875f;
            tmpy = (y1 - cy1 * 2 + cy2) * 0.1875f;
            dddfx = ((cx1 - cx2) * 3 - x1 + x2) * 0.09375f;
            dddfy = ((cy1 - cy2) * 3 - y1 + y2) * 0.09375f;
            ddfx = tmpx * 2 + dddfx;
            ddfy = tmpy * 2 + dddfy;
            dfx = (cx1 - x1) * 0.75f + tmpx + dddfx * 0.16666667f;
            dfy = (cy1 - y1) * 0.75f + tmpy + dddfy * 0.16666667f;
            pathLength += (float)sqrt(dfx * dfx + dfy * dfy);
            dfx += ddfx;
            dfy += ddfy;
            ddfx += dddfx;
            ddfy += dddfy;
            pathLength += (float)sqrt(dfx * dfx + dfy * dfy);
            dfx += ddfx;
            dfy += ddfy;
            pathLength += (float)sqrt(dfx * dfx + dfy * dfy);
            dfx += ddfx + dddfx;
            dfy += ddfy + dddfy;
            pathLength += (float)sqrt(dfx * dfx + dfy * dfy);
            _curves[i] = pathLength;
            x1 = x2;
            y1 = y2;
        }
        
        if (percentPosition)
        {
            position *= pathLength;
        }
        
        if (percentSpacing)
        {
            for (int i = 0; i < spacesCount; ++i)
            {
                _spaces[i] *= pathLength;
            }
        }
        
        float curveLength = 0;
        for (int i = 0, o = 0, curve = 0, segment = 0; i < spacesCount; i++, o += 3)
        {
            float space = _spaces[i];
            position += space;
            float p = position;
            
            if (closed)
            {
                p = fmod(p, pathLength);
                
                if (p < 0)
                {
                    p += pathLength;
                }
                curve = 0;
            }
            else if (p < 0)
            {
                addBeforePosition(p, _world, 0, _positions, o);
                continue;
            }
            else if (p > pathLength)
            {
                addAfterPosition(p - pathLength, _world, verticesLength - 4, _positions, o);
                continue;
            }
            
            // Determine curve containing position.
            for (;; curve++)
            {
                float length = _curves[curve];
                if (p > length)
                {
                    continue;
                }
                
                if (curve == 0)
                {
                    p /= length;
                }
                else
                {
                    float prev = _curves[curve - 1];
                    p = (p - prev) / (length - prev);
                }
                break;
            }
            
            // Curve segment lengths.
            if (curve != prevCurve)
            {
                prevCurve = curve;
                int ii = curve * 6;
                x1 = _world[ii];
                y1 = _world[ii + 1];
                cx1 = _world[ii + 2];
                cy1 = _world[ii + 3];
                cx2 = _world[ii + 4];
                cy2 = _world[ii + 5];
                x2 = _world[ii + 6];
                y2 = _world[ii + 7];
                tmpx = (x1 - cx1 * 2 + cx2) * 0.03f;
                tmpy = (y1 - cy1 * 2 + cy2) * 0.03f;
                dddfx = ((cx1 - cx2) * 3 - x1 + x2) * 0.006f;
                dddfy = ((cy1 - cy2) * 3 - y1 + y2) * 0.006f;
                ddfx = tmpx * 2 + dddfx;
                ddfy = tmpy * 2 + dddfy;
                dfx = (cx1 - x1) * 0.3f + tmpx + dddfx * 0.16666667f;
                dfy = (cy1 - y1) * 0.3f + tmpy + dddfy * 0.16666667f;
                curveLength = (float)sqrt(dfx * dfx + dfy * dfy);
                _segments[0] = curveLength;
                for (ii = 1; ii < 8; ii++)
                {
                    dfx += ddfx;
                    dfy += ddfy;
                    ddfx += dddfx;
                    ddfy += dddfy;
                    curveLength += (float)sqrt(dfx * dfx + dfy * dfy);
                    _segments[ii] = curveLength;
                }
                dfx += ddfx;
                dfy += ddfy;
                curveLength += (float)sqrt(dfx * dfx + dfy * dfy);
                _segments[8] = curveLength;
                dfx += ddfx + dddfx;
                dfy += ddfy + dddfy;
                curveLength += (float)sqrt(dfx * dfx + dfy * dfy);
                _segments[9] = curveLength;
                segment = 0;
            }
            
            // Weight by segment length.
            p *= curveLength;
            for (;; segment++)
            {
                float length = _segments[segment];
                if (p > length)
                {
                    continue;
                }
                
                if (segment == 0)
                {
                    p /= length;
                }
                else
                {
                    float prev = _segments[segment - 1];
                    p = segment + (p - prev) / (length - prev);
                }
                break;
            }
            addCurvePosition(p * 0.1f, x1, y1, cx1, cy1, cx2, cy2, x2, y2, _positions, o, tangents || (i > 0 && space < EPSILON));
        }
        
        return _positions;
    }
    
    void PathConstraint::addBeforePosition(float p, Vector<float>& temp, int i, Vector<float>& output, int o)
    {
        float x1 = temp[i];
        float y1 = temp[i + 1];
        float dx = temp[i + 2] - x1;
        float dy = temp[i + 3] - y1;
        float r = MathUtil::atan2(dy, dx);
        
        output[o] = x1 + p * MathUtil::cos(r);
        output[o + 1] = y1 + p * MathUtil::sin(r);
        output[o + 2] = r;
    }
    
    void PathConstraint::addAfterPosition(float p, Vector<float>& temp, int i, Vector<float>& output, int o)
    {
        float x1 = temp[i + 2];
        float y1 = temp[i + 3];
        float dx = x1 - temp[i];
        float dy = y1 - temp[i + 1];
        float r = MathUtil::atan2(dy, dx);
        output[o] = x1 + p * MathUtil::cos(r);
        output[o + 1] = y1 + p * MathUtil::sin(r);
        output[o + 2] = r;
    }
    
    void PathConstraint::addCurvePosition(float p, float x1, float y1, float cx1, float cy1, float cx2, float cy2, float x2, float y2, Vector<float>& output, int o, bool tangents)
    {
        if (p < EPSILON || isnan(p))
        {
            p = EPSILON;
        }
        
        float tt = p * p, ttt = tt * p, u = 1 - p, uu = u * u, uuu = uu * u;
        float ut = u * p, ut3 = ut * 3, uut3 = u * ut3, utt3 = ut3 * p;
        float x = x1 * uuu + cx1 * uut3 + cx2 * utt3 + x2 * ttt, y = y1 * uuu + cy1 * uut3 + cy2 * utt3 + y2 * ttt;
        output[o] = x;
        output[o + 1] = y;
        if (tangents)
        {
            output[o + 2] = (float)atan2(y - (y1 * uu + cy1 * ut * 2 + cy2 * tt), x - (x1 * uu + cx1 * ut * 2 + cx2 * tt));
        }
    }
}
