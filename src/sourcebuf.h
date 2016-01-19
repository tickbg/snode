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
class sourcebuf : public async_streambuf<SourceImpl::char_type, sourcebuf<SourceImpl>>
{
private:
    SourceImpl& source_;

    // internal buffered data from source
    struct buffer_info
    {
        std::vector<char_type> data_; // actual buffer storage to hold the data
        size_t rdpos_;                // Buffer read pointer as a offset from the source // start of the buffer
        size_t off_;                  // Source position that the start of the buffer represents.
        size_t fill_;                 // Amount of file data actually in the buffer
    };

    buffer_info buf_;
public:
    typedef async_streambuf<SourceImpl::char_type, sourcebuf<SourceImpl>> base_stream_type;

    sourcebuf(SourceImpl& source) : base_stream_type(std::ios_base::in), source_(source), current_position_(0)
    {}

    ~sourcebuf()
    {
        this->close();
    }

    /// implementation of can_seek() to be used in async_streambuf
    bool can_seek() const { return this->is_open(); }

    /// implementation of has_size() to be used in async_streambuf
    bool has_size() const { return this->is_open(); }

    /// implementation of buffer_size() to be used in async_streambuf
    size_t buffer_size(std::ios_base::openmode = std::ios_base::in) const
    {
        return buf_.data_.size();
    }

    /// implementation of in_avail() to be used in async_streambuf
    size_t in_avail() const
    {
        if (!this->is_open()) return 0;

        if (buf_.data_.empty() || 0 == buf_.fill_) return 0;


        //if (buf_.off_ > buf_.rdpos_ || (m_info->m_bufoff+m_info->m_buffill) < m_info->m_rdpos ) return 0;

        /*
        msl::safeint3::SafeInt<size_t> rdpos(m_info->m_rdpos);
        msl::safeint3::SafeInt<size_t> buffill(m_info->m_buffill);
        msl::safeint3::SafeInt<size_t> bufpos = rdpos - m_info->m_bufoff;

        return buffill - bufpos;
        */

        // See the comment in seek around the restriction that we do not allow read head to
        // seek beyond the current write_end.
        assert(current_position_ <= source_.size());

        size_t readhead(current_position_);
        size_t write_end(data_.size());
        return (size_t)(write_end - readhead);
        return 0;
    }

    /// Sets the stream buffer implementation to buffer or not buffer.
    /// implementation of set_buffer_size() to be used in async_streambuf
    void set_buffer_size(size_t size, std::ios_base::openmode = std::ios_base::in)
    {
        if (std::ios_base::in != openmode)
            return;
        buf_.data_.reserve(size);
    }

    /// implementation of sync() to be used in async_streambuf
    bool sync() { return (true); }

    /// implementation of putc() to be used in async_streambuf
    template<typename WriteHandler>
    void putc(char_type ch, WriteHandler handler)
    {
        async_task::connect(handler, traits::eof());
    }

    /// implementation of putn() to be used in async_streambuf
    template<typename WriteHandler>
    void putn(char_type* ptr, size_t count, WriteHandler handler)
    {
        async_task::connect(handler, 0);
    }

    /// implementation of alloc() to be used in async_streambuf
    char_type* alloc(size_t count) { return nullptr; }

    /// implementation of commit() to be used in async_streambuf
    void commit(size_t count) { return; }

    /// implementation of acquire() to be used in async_streambuf
    bool acquire(CharType*& ptr, size_t& count)
    {
        ptr = nullptr;
        count = 0;
        if (!this->can_read())
            return false;

        count = in_avail();
        if (count > 0)
        {
            ptr = (char_type*)&data_[current_position_];
            return true;
        }
        else
        {
            // Can only be open for read OR write, not both. If there is no data then
            // we have reached the end of the stream so indicate such with true.
            return false;
        }
    }

    /// internal implementation of release() from async_streambuf
    void release(char_type* ptr, size_t count)
    {
        if (nullptr != ptr)
            update_current_position(current_position_ + count);
    }

    /// internal implementation of getn() from async_streambuf
    template<typename ReadHandler>
    void getn(char_type* ptr, size_t count, ReadHandler handler)
    {
        int_type res = this->read(ptr, count);
        async_task::connect(handler, res);
    }

    /// internal implementation of sgetn() from async_streambuf
    size_t sgetn(char_type* ptr, size_t count)
    {
        return this->read(ptr, count);
    }

    /// internal implementation of scopy() from async_streambuf
    size_t scopy(char_type* ptr, size_t count)
    {
        return this->read(ptr, count, false);
    }

    /// internal implementation of bumpc() from async_streambuf
    template<typename ReadHandler>
    void bumpc(ReadHandler handler)
    {
        int_type res = this->read_byte(true);
        async_task::connect(handler, res);
    }

    /// internal implementation of sbumpc() from async_streambuf
    int_type sbumpc()
    {
        return this->read_byte(true);
    }

    /// internal implementation of getc() from async_streambuf
    template<typename ReadHandler>
    void getc(ReadHandler handler)
    {
        int_type res = this->read_byte(false);
        async_task::connect(handler, res);
    }

    /// internal implementation of sgetc() from async_streambuf
    int_type sgetc()
    {
        return this->read_byte(false);
    }

    /// internal implementation of nextc() from async_streambuf
    template<typename ReadHandler>
    void nextc(ReadHandler handler)
    {
        int_type res = this->read_byte(true);
        async_task::connect(handler, res);
    }

    /// internal implementation of ungetc() from async_streambuf
    template<typename ReadHandler>
    void ungetc(ReadHandler handler)
    {
        /*
        auto pos = seekoff(-1, std::ios_base::cur, std::ios_base::in);
        if ( pos == (pos_type)traits::eof())
            async_task::connect(handler, static_cast<int_type>(traits::eof()));
        int_type res = this->getc();
        */
    }

    /// internal implementation of getpos() from async_streambuf
    pos_type getpos(std::ios_base::openmode mode = std::ios_base::in) const
    {
        if ((mode & std::ios_base::in) || !this->can_read())
        {
            return static_cast<pos_type>(traits::eof());
        }
        return static_cast<pos_type>(current_position_);
    }

    /// Seeks to the given position implementation.
    pos_type seekpos(pos_type position, std::ios_base::openmode mode = std::ios_base::in)
    {
        pos_type beg(0);

        // In order to support relative seeking from the end position we need to fix an end position.
        // Technically, there is no end for the stream buffer as new writes would just expand the buffer.
        // For now, we assume that the current write_end is the end of the buffer. We use this artificial
        // end to restrict the read head from seeking beyond what is available.

        pos_type end(data_.size());

        if (position >= beg)
        {
            auto pos = static_cast<size_t>(position);

            // Read head
            if ((mode & std::ios_base::in) && this->can_read())
            {
                if (position <= end)
                {
                    // We do not allow reads to seek beyond the end or before the start position.
                    update_current_position(pos);
                    return static_cast<pos_type>(current_position_);
                }
            }

            /*
            // Write head
            if ((mode & std::ios_base::out) && this->can_write())
            {
                // Allocate space
                resize_for_write(pos);

                // Nothing to really copy

                // Update write head and satisfy read requests if any
                update_current_position(pos);

                return static_cast<pos_type>(current_position_);
            }
            */
        }

        return static_cast<pos_type>(traits::eof());
    }

    /// Seeks to a position given by a relative offset implementation.
    pos_type seekoff(off_type offset, std::ios_base::seekdir way, std::ios_base::openmode mode)
    {
        pos_type beg = 0;
        pos_type cur = static_cast<pos_type>(current_position_);
        pos_type end = static_cast<pos_type>(data_.size());

        switch ( way )
        {
        case std::ios_base::beg:
            return seekpos(beg + offset, mode);

        case std::ios_base::cur:
            return seekpos(cur + offset, mode);

        case std::ios_base::end:
            return seekpos(end + offset, mode);

        default:
            return static_cast<pos_type>(traits::eof());
        }
    }

    void close_read()
    {
        this->stream_can_read_ = false;
    }

    void close_write()
    {
        // noop
    }
};

}}

#endif /* _SOURCEBUF_H_ */
