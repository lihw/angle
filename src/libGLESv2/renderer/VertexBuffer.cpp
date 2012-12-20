//
// Copyright (c) 2002-2012 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// VertexBuffer.cpp: Defines the abstract VertexBuffer class and VertexBufferInterface
// class with derivations, classes that perform graphics API agnostic vertex buffer operations.

#include "libGLESv2/renderer/VertexBuffer.h"

#include "libGLESv2/renderer/Renderer9.h"

namespace rx
{

unsigned int VertexBuffer::mNextSerial = 1;

VertexBuffer::VertexBuffer()
{
    updateSerial();
}

VertexBuffer::~VertexBuffer()
{
}

void VertexBuffer::updateSerial()
{
    mSerial = mNextSerial++;
}

unsigned int VertexBuffer::getSerial() const
{
    return mSerial;
}


unsigned int VertexBufferInterface::mCurrentSerial = 1;

VertexBufferInterface::VertexBufferInterface(rx::Renderer9 *renderer, std::size_t size, DWORD usageFlags) : mRenderer(renderer), mVertexBuffer(NULL)
{
    if (size > 0)
    {
        // D3D9_REPLACE
        HRESULT result = mRenderer->createVertexBuffer(size, usageFlags,&mVertexBuffer);
        mSerial = issueSerial();

        if (FAILED(result))
        {
            ERR("Out of memory allocating a vertex buffer of size %lu.", size);
        }
    }

    mBufferSize = size;
    mWritePosition = 0;
    mRequiredSpace = 0;
}

VertexBufferInterface::~VertexBufferInterface()
{
    if (mVertexBuffer)
    {
        mVertexBuffer->Release();
    }
}

void VertexBufferInterface::unmap()
{
    if (mVertexBuffer)
    {
        mVertexBuffer->Unlock();
    }
}

IDirect3DVertexBuffer9 *VertexBufferInterface::getBuffer() const
{
    return mVertexBuffer;
}

unsigned int VertexBufferInterface::getSerial() const
{
    return mSerial;
}

unsigned int VertexBufferInterface::issueSerial()
{
    return mCurrentSerial++;
}

void VertexBufferInterface::addRequiredSpace(UINT requiredSpace)
{
    mRequiredSpace += requiredSpace;
}

StreamingVertexBufferInterface::StreamingVertexBufferInterface(rx::Renderer9 *renderer, std::size_t initialSize) : VertexBufferInterface(renderer, initialSize, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY)
{
}

StreamingVertexBufferInterface::~StreamingVertexBufferInterface()
{
}

void *StreamingVertexBufferInterface::map(const gl::VertexAttribute &attribute, std::size_t requiredSpace, std::size_t *offset)
{
    void *mapPtr = NULL;

    if (mVertexBuffer)
    {
        HRESULT result = mVertexBuffer->Lock(mWritePosition, requiredSpace, &mapPtr, D3DLOCK_NOOVERWRITE);

        if (FAILED(result))
        {
            ERR("Lock failed with error 0x%08x", result);
            return NULL;
        }

        *offset = mWritePosition;
        mWritePosition += requiredSpace;
    }

    return mapPtr;
}

void StreamingVertexBufferInterface::reserveRequiredSpace()
{
    if (mRequiredSpace > mBufferSize)
    {
        if (mVertexBuffer)
        {
            mVertexBuffer->Release();
            mVertexBuffer = NULL;
        }

        mBufferSize = std::max(mRequiredSpace, 3 * mBufferSize / 2);   // 1.5 x mBufferSize is arbitrary and should be checked to see we don't have too many reallocations.

        // D3D9_REPLACE
        HRESULT result = mRenderer->createVertexBuffer(mBufferSize, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, &mVertexBuffer);
        mSerial = issueSerial();

        if (FAILED(result))
        {
            ERR("Out of memory allocating a vertex buffer of size %lu.", mBufferSize);
        }

        mWritePosition = 0;
    }
    else if (mWritePosition + mRequiredSpace > mBufferSize)   // Recycle
    {
        if (mVertexBuffer)
        {
            void *dummy;
            mVertexBuffer->Lock(0, 1, &dummy, D3DLOCK_DISCARD);
            mVertexBuffer->Unlock();
        }

        mWritePosition = 0;
    }

    mRequiredSpace = 0;
}

StaticVertexBufferInterface::StaticVertexBufferInterface(rx::Renderer9 *renderer) : VertexBufferInterface(renderer, 0, D3DUSAGE_WRITEONLY)
{
}

StaticVertexBufferInterface::~StaticVertexBufferInterface()
{
}

void *StaticVertexBufferInterface::map(const gl::VertexAttribute &attribute, std::size_t requiredSpace, std::size_t *streamOffset)
{
    void *mapPtr = NULL;

    if (mVertexBuffer)
    {
        HRESULT result = mVertexBuffer->Lock(mWritePosition, requiredSpace, &mapPtr, 0);

        if (FAILED(result))
        {
            ERR("Lock failed with error 0x%08x", result);
            return NULL;
        }

        int attributeOffset = attribute.mOffset % attribute.stride();
        VertexElement element = {attribute.mType, attribute.mSize, attribute.stride(), attribute.mNormalized, attributeOffset, mWritePosition};
        mCache.push_back(element);

        *streamOffset = mWritePosition;
        mWritePosition += requiredSpace;
    }

    return mapPtr;
}

void StaticVertexBufferInterface::reserveRequiredSpace()
{
    if (!mVertexBuffer && mBufferSize == 0)
    {
        // D3D9_REPLACE
        HRESULT result = mRenderer->createVertexBuffer(mRequiredSpace, D3DUSAGE_WRITEONLY, &mVertexBuffer);
        mSerial = issueSerial();

        if (FAILED(result))
        {
            ERR("Out of memory allocating a vertex buffer of size %lu.", mRequiredSpace);
        }

        mBufferSize = mRequiredSpace;
    }
    else if (mVertexBuffer && mBufferSize >= mRequiredSpace)
    {
        // Already allocated
    }
    else UNREACHABLE();   // Static vertex buffers can't be resized

    mRequiredSpace = 0;
}

std::size_t StaticVertexBufferInterface::lookupAttribute(const gl::VertexAttribute &attribute)
{
    for (unsigned int element = 0; element < mCache.size(); element++)
    {
        if (mCache[element].type == attribute.mType &&
            mCache[element].size == attribute.mSize &&
            mCache[element].stride == attribute.stride() &&
            mCache[element].normalized == attribute.mNormalized)
        {
            if (mCache[element].attributeOffset == attribute.mOffset % attribute.stride())
            {
                return mCache[element].streamOffset;
            }
        }
    }

    return -1;
}

}