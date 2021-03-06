#include "ImageBuffer.h"

#include <cassert>
#include <png.h>
#include "Utils.h"

ImageBuffer::ImageBuffer()
    : m_width(0), m_height(0)
{}

ImageBuffer::ImageBuffer(size_t width, size_t height)
	: m_width(width), m_height(height)
{
    m_pixels.assign(width * height, 0);
    m_priority.assign(width * height, 0);
}

void ImageBuffer::Clear()
{
    std::fill(m_pixels.begin(), m_pixels.end(), 0);
    std::fill(m_priority.begin(), m_priority.end(), 0);
}

void ImageBuffer::Resize(size_t width, size_t height)
{
    m_width = width;
    m_height = height;
    m_pixels.clear();
    m_pixels.assign(m_width * m_height, 0);
    m_priority.assign(width * height, 0);
}

void ImageBuffer::InsertTile(size_t x, size_t y, uint8_t palette_index, const Tile& tile, const Tileset& tileset)
{
    size_t max_x = x + 7;
    size_t max_y = y + 7;
    if ((max_x >= m_width) || (max_y >= m_height))
    {
        std::ostringstream ss;
        ss << "Attempt to draw tile in out-of-range position " << x << ", " << y 
           << " : The image buffer is only " << m_width << " x " << m_height << " pixels." << std::endl;
        Debug(ss.str());
    }
    else
    {
	    auto tile_bits = tileset.getTile(tile);
	    const uint8_t pal_bits = palette_index << 4;
        size_t begin_offset = y * m_width + x;
        auto row_it = m_pixels.begin() + begin_offset;
        auto dest_it = row_it;
        auto pri_row_it = m_priority.begin() + begin_offset;
        auto pri_dest_it = row_it;
        uint8_t priority = tile.Attributes().getAttribute(TileAttributes::ATTR_PRIORITY);
        for (size_t i = 0; i < tile_bits.size(); ++i)
        {
            if (i % 8 == 0)
            {
                dest_it = row_it + m_width * (i / 8);
                pri_dest_it = pri_row_it + m_width * (i / 8);
            }
            if (tile_bits[i] != 0)
            {
                *dest_it = tile_bits[i] | pal_bits;
                *pri_dest_it = priority;
            }
            dest_it++;
            pri_dest_it++;
        }
    }
}

bool ImageBuffer::WritePNG(const std::string& filename, const std::vector<Palette>& palettes)
{
    bool retval = false;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) abort();

    png_set_IHDR(
        png,
        info,
        m_width, m_height,
        8,
        PNG_COLOR_TYPE_PALETTE,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE,
        PNG_FILTER_TYPE_BASE
    );

    png_color png_palette[256] = { 0 };
    png_byte png_alpha[256] = { 0 };
    
    size_t entry = 0;
    for (const auto& pal : palettes)
    {
        for (size_t i = 0; i < 16; ++i)
        {
            png_palette[entry + i].red   = pal.getR(i);
            png_palette[entry + i].green = pal.getG(i);
            png_palette[entry + i].blue  = pal.getB(i);
            png_alpha[entry + i]         = pal.getA(i);
        }
        entry += 16;
    }
    png_set_PLTE(png, info, png_palette, 256);
    png_set_tRNS(png, info, png_alpha, 256, NULL);

    FILE* fp = fopen(filename.c_str(), "wb");

    if (fp != NULL)
    {
        png_init_io(png, fp);
        png_write_info(png, info);

        const uint8_t* row = m_pixels.data();
        for (size_t y = 0; y < m_height; ++y)
        {
            png_write_row(png, row);
            row += m_width;
        }

        png_write_end(png, info);
        fclose(fp);
        retval = true;
    }
    else
    {
        Debug("Unable to open PNG!");
    }

    png_destroy_write_struct(&png, &info);

    return retval;
}

void ImageBuffer::InsertBlock(size_t x, size_t y, uint8_t palette_index, const BigTile& block, const Tileset& tileset)
{
    if ((y + 7) * m_width + x + 7 < m_pixels.size())
    {
        InsertTile(x, y, palette_index, block.getTile(0), tileset);
        InsertTile(x + 8, y, palette_index, block.getTile(1), tileset);
        InsertTile(x, y + 8, palette_index, block.getTile(2), tileset);
        InsertTile(x + 8, y + 8, palette_index, block.getTile(3), tileset);
    }
    else
    {
        assert(true);
        Debug("Coordinates out of range");
    }
}

const std::vector<uint8_t>& ImageBuffer::GetRGB(const std::vector<Palette>& pals) const
{
	m_rgb.resize(m_width * m_height * 3);
    auto it = m_rgb.begin();
	for (const auto& pixel : m_pixels)
	{
        *it++ = (pals[pixel >> 4].getR(pixel & 0x0F));
        *it++ = (pals[pixel >> 4].getG(pixel & 0x0F));
        *it++ = (pals[pixel >> 4].getB(pixel & 0x0F));
	}
	return m_rgb;
}

const std::vector<uint8_t>& ImageBuffer::GetAlpha(const std::vector<Palette>& pals, uint8_t low_pri_max_opacity, uint8_t high_pri_max_opacity) const
{
    m_alpha.resize(m_width * m_height);
    auto pri = m_priority.cbegin();
    auto it = m_alpha.begin();
	for (const auto& pixel : m_pixels)
	{
        uint8_t alpha = pals[pixel >> 4].getA(pixel & 0x0F);
        uint8_t max_opacity = *pri++ ? high_pri_max_opacity : low_pri_max_opacity;
        *it++ = std::min(max_opacity, alpha);
	}
	return m_alpha;
}

std::shared_ptr<wxBitmap> ImageBuffer::MakeBitmap(const std::vector<Palette>& pals, bool use_alpha, uint8_t low_pri_max_opacity, uint8_t high_pri_max_opacity) const
{
    GetRGB(pals);
    wxImage img(m_width, m_height, m_rgb.data(), true);
    if (use_alpha)
    {
        GetAlpha(pals, low_pri_max_opacity, high_pri_max_opacity);
        img.SetAlpha(m_alpha.data(), true);
    }
    auto ret = std::make_shared<wxBitmap>(img);
    return ret;
}

size_t ImageBuffer::GetHeight() const
{
	return m_height;
}

size_t ImageBuffer::GetWidth() const
{
	return m_width;
}
