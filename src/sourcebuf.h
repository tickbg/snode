//
// sourcebuf.h
// Copyright (C) 2016  Emil Penchev, Bulgaria

#ifndef _SOURCEBUF_H_
#define _SOURCEBUF_H_

#include "async_streams.h"
#include <cstdint>

namespace snode
{
namespace streams
{

/// The sourcebuf class serves as a memory-based stream buffer that supports only reading
/// sequences of characters from a arbitrary static source object that complies with SourceImpl interface.
/// SourceImpl can be anything not depending from the medium (file, memory, network ..)
template<typename SourceImpl>
class sourcebuf : public async_streambuf<typename SourceImpl::char_type, sourcebuf<SourceImpl>>
{
private:
    SourceImpl& source_;
    typedef typename SourceImpl::char_type char_type;
    typedef async_streambuf<char_type, sourcebuf<SourceImpl>> base_stream_type;
    typedef typename sourcebuf::traits traits;
    typedef typename sourcebuf::pos_type pos_type;
    typedef typename sourcebuf::int_type int_type;
    typedef typename sourcebuf::off_type off_type;

    // internal buffered data from source
    struct buffer_info
    {
        buffer_info(size_t buffer_size) :
            rdpos_(0),
            bufoff_(0),
            buffill_(0),
            atend_(false),
            buffer_(buffer_size)
        {}

        size_t rdpos_;                  // Read pointer as an offset from the start of the source.
        size_t bufoff_;                 // Source position that the start of the buffer represents.
        size_t buffill_;                // Amount of file data actually in the buffer (how much buffer is filled)
        bool   atend_;                  // End indicator flag
        std::vector<char_type> buffer_;
    };

    /// container used for async operations
    template<typename Handler>
    struct read_op
    {
    public:
        read_op(sourcebuf<SourceImpl>& buf, Handler h) : handler_(h), buf_(buf)
        {}

        void read(char_type* ptr, size_t count, bool advance = true)
        {
            auto countr = buf_.read(ptr, count);
            handler_(countr);
        }

        void read_byte(bool advance = true)
        {
            auto chr = buf_.read_byte();
            handler_(chr);
        }
    private:
        Handler handler_;
        sourcebuf<SourceImpl>& buf_;
    };

    template<typename Handler> friend class read_op;
    buffer_info info_;

    /// Fills buffer with data (count characters) from the source.
    /// Note: buffer is filled only when all data is read from it.
    /// Returns count characters read from source or 0 if there is nothing to read.
    size_t fill_buffer(size_t count, off_type offset = -1)
    {
        size_t totalr = 0;
        size_t countr = 0;
        auto charSize = sizeof(char_type);

        // check if fill count is actually bigger than what buffer can hold
        if (count > info_.buffer_.size())
            countr = info_.buffer_.size();
        else
            countr = count;

        totalr = source_.read(info_.buffer_.data(), countr, offset);
        info_.atend_ = (countr > totalr);
        if (totalr)
        {
            if (offset)
                info_.rdpos_ = offset;

            info_.bufoff_ = info_.rdpos_;
            info_.buffill_ = totalr;
        }

        return totalr;
    }

    /// Adjust the internal buffers and pointers when the application seeks to a new read location in the stream.
    size_t seekrdpos(size_t pos)
    {
        if ( pos < info_.bufoff_ || pos > (info_.bufoff_ + info_.buffill_) )
        {
            info_.bufoff_ = info_.buffill_ = 0;
        }

        info_.rdpos_ = pos;
        return info_.rdpos_;
    }

    /// Reads a byte from the stream and returns it as int_type.
    /// Note: This routine shall only be called if can_satisfy() returned true.
    int_type read_byte(bool advance = true)
    {
        if (in_avail() > 0)
        {

        }
        char_type value;
        auto read_size = this->read(&value, 1);
        return read_size == 1 ? static_cast<int_type>(value) : traits::eof();
    }

    /// Reads up to (count) characters into (ptr) and returns the count of characters copied.
    /// The return value (actual characters copied) could be <= count.
    /// Note: This routine shall only be called if can_satisfy() returned true.
    size_t read(char_type* ptr, size_t count, bool advance = true)
    {
        size_t totalr = 0;
        size_t charSize = sizeof(char_type);
        auto bufoff = info_.rdpos_ - info_.bufoff_;

        if ( in_avail() >= count )
        {
            std::memcpy((void *)ptr, info_.buffer_.data() + bufoff, count * charSize);
            totalr = count;
            if (advance)
                info_.rdpos_ += count;
        }
        else
        {
            // reads only what is available
            auto avail = in_avail();
            auto fillcount = count - avail;
            if (avail)
            {
                std::memcpy((void *)ptr, info_.buffer_.data() + bufoff, avail * charSize);
                totalr = avail;
                if (advance)
                    info_.rdpos_ += avail;
            }

            do
            {
                // buffer is filled after all data has been read from it
                avail = fill_buffer(fillcount);
                if (avail)
                {
                    auto charCount = (avail >= fillcount ? fillcount : avail);
                    fillcount = (fillcount > avail ? fillcount - avail : 0);

                    std::memcpy((void *)ptr + (totalr * charSize), info_.buffer_.data(), charCount * charSize);
                    totalr += charCount;
                    if (advance)
                        info_.rdpos_ += totalr;
                }

            } while (fillcount && avail);
        }
        return totalr;
    }

public:
    sourcebuf(SourceImpl& source) : base_stream_type(std::ios_base::in), source_(source), info_(512)
    {}

    ~sourcebuf()
    {
        this->close();
    }

    /// can_seek() is used to determine whether a stream buffer supports seeking.
    bool can_seek() const { return this->is_open(); }

    /// has_size() is used to determine whether a stream buffer supports size().
    bool has_size() const { return this->is_open(); }

    /// Gets the stream buffer size only for in direction otherwise 0 is returned.
    size_t buffer_size(std::ios_base::openmode direction = std::ios_base::in) const
    {
        if ( std::ios_base::in == direction )
            return info_.buffer_.size();
        else
            return 0;
    }

    /// Returns the number of characters that are immediately available to be consumed without blocking.
    /// For details see async_streambuf::in_avail()
    size_t in_avail() const
    {
        if (!this->is_open()) return 0;

        if (0 == info_.buffill_) return 0;
        if (info_.bufoff_ > info_.rdpos_ || (info_.bufoff_ + info_.buffill_) < info_.rdpos_) return 0;

        size_t rdpos(info_.rdpos_);
        size_t buffill(info_.buffill_);
        size_t bufpos = rdpos - info_.bufoff_;

        return buffill - bufpos;
    }

    /// Sets the stream buffer implementation to buffer or not buffer.
    /// For details see async_streambuf::set_buffer_size()
    void set_buffer_size(size_t size, std::ios_base::openmode direction = std::ios_base::in)
    {
        if (std::ios_base::in != direction)
            return;
        info_.buffer_.reserve(size);
    }

    /// implementation of getn() to be used in async_streambuf
    template<typename ReadHandler>
    void getn(char_type* ptr, size_t count, ReadHandler handler)
    {
        read_op<ReadHandler> op(*this, handler);
        async_task::connect(&read_op<ReadHandler>::read, op, ptr, count);
    }

    /// implementation of sgetn() to be used in async_streambuf
    size_t sgetn(char_type* ptr, size_t count)
    {
        return this->read(ptr, count);
    }

    /// implementation of scopy() to be used in async_streambuf
    size_t scopy(char_type* ptr, size_t count)
    {
        return this->read(ptr, count);
    }

    /// implementation of bumpc() to be used in async_streambuf
    template<typename ReadHandler>
    void bumpc(ReadHandler handler)
    {
        read_op<ReadHandler> op(*this, handler);
        async_task::connect(&read_op<ReadHandler>::read_byte, op);
    }

    /// implementation of sbumpc() to be used in async_streambuf
    int_type sbumpc()
    {
        return this->read_byte();
    }

    /// implementation of getc() to be used in async_streambuf
    template<typename ReadHandler>
    void getc(ReadHandler handler)
    {
        bool advance = false;
        read_op<ReadHandler> op(*this, handler);
        async_task::connect(&read_op<ReadHandler>::read_byte, op, advance);
    }

    /// implementation of sgetc() to be used in async_streambuf
    int_type sgetc()
    {
        return this->read_byte(false);
    }

    /// implementation of nextc() to be used in async_streambuf
    template<typename ReadHandler>
    void nextc(ReadHandler handler)
    {
        auto pos = seekoff(1, std::ios_base::cur, std::ios_base::in);
        if (pos == (pos_type)traits::eof())
            async_task::connect(handler, static_cast<int_type>(traits::eof()));
        else
            this->getc(handler);
    }

    /// implementation of ungetc() to be used in async_streambuf
    template<typename ReadHandler>
    void ungetc(ReadHandler handler)
    {
        auto pos = seekoff(-1, std::ios_base::cur, std::ios_base::in);
        if (pos == (pos_type)traits::eof())
            async_task::connect(handler, static_cast<int_type>(traits::eof()));
        else
            this->getc(handler);
    }

    /// implementation of getpos() to be used in async_streambuf
    pos_type getpos(std::ios_base::openmode mode = std::ios_base::in) const
    {
        if ((std::ios_base::in != mode) || !this->can_read())
            return static_cast<pos_type>(traits::eof());

        return this->seekoff(0, std::ios_base::cur, mode);
    }

    /// Seeks to the given position implementation.
    pos_type seekpos(pos_type position, std::ios_base::openmode mode = std::ios_base::in)
    {
        if (std::ios_base::in != mode || !this->can_read())
            return static_cast<pos_type>(traits::eof());

        pos_type beg(0);
        pos_type end(source_.size());

        // We do not allow reads to seek beyond the end or before the start position.
        if (position >= beg)
        {
            auto pos = static_cast<size_t>(position);
            if (position <= end)
            {
                auto bufend = info_.bufoff_ + info_.buffill_;
                // if new position is not in buffer ranges, buffer is flushed and refilled.
                if (bufend < pos || info_.bufoff_ > pos)
                    info_.rdpos_ = pos;
                else
                    fill_buffer(info_.buffer_.size(), pos);

                return static_cast<pos_type>(info_.rdpos_);
            }
        }
        return static_cast<pos_type>(traits::eof());
    }

    /// Seeks to a position given by a relative offset implementation.
    pos_type seekoff(off_type offset, std::ios_base::seekdir way, std::ios_base::openmode mode)
    {
        if ((std::ios_base::in != mode) || !this->can_read())
            return static_cast<pos_type>(traits::eof());

        pos_type beg = 0;
        pos_type cur = static_cast<pos_type>(info_.rdpos_);
        pos_type end = static_cast<pos_type>(source_.size());

        if (std::ios_base::beg == way)
            return seekpos(beg + offset, mode);
        else if (std::ios_base::cur == way)
            return seekpos(cur + offset, mode);
        else if (std::ios_base::end == way)
            return seekpos(end + offset, mode);
        else
            return static_cast<pos_type>(traits::eof());

        return 0;
    }

    void close_read()
    {
        this->stream_can_read_ = false;
        source_.close();
    }

    void close_write()
    {}
};

}}

#endif /* _SOURCEBUF_H_ */
