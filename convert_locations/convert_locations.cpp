//
// open horizon -- undefined_darkness@outlook.com
//

#include "util/util.h"

#include "containers/cdp.h"
#include "containers/pac5.h"
#include "containers/pac6.h"
#include "containers/poc.h"
#include "containers/fhm.h"
#include "formats/tga.h"
#include "renderer/texture_gim.h"
#include "renderer/mesh_ndxr.h"
#include "renderer/mesh_sm.h"
#include "render/bitmap.h"
#include "zip.h"
#include "util/xml.h"
#include "dxt_util.h"
#include "obj_writer.h"
#include <set>
#include <list>

#ifndef _WIN32
    #include <unistd.h>
#endif

//------------------------------------------------------------

const std::string version = "0.1";

//------------------------------------------------------------

inline std::string base_name(std::string base, int idx) { return base + (idx < 100 ? "0" : "" ) + (idx < 10 ? "0" : "" ) + std::to_string(idx); }
inline std::string tex_name(std::string base, int idx, std::string extension = "tga") { return base_name(base, idx) + "." + extension; }
inline std::string loc_name(std::string base, int idx) { return base_name(base + " ", idx); }
inline std::string loc_filename(std::string base, int idx) { return base_name(base + "_loc", idx) + ".zip"; }

template<typename t> nya_memory::tmp_buffer_ref load_resource(const t &p, int idx)
{
    nya_memory::tmp_buffer_ref b(p.get_chunk_size(idx));
    if (!p.read_chunk_data(idx, b.get_data()))
        b.free();

    return b;
}

//------------------------------------------------------------

template<typename t> bool write_entry(const t &p, int idx, std::string name, zip_t *zip)
{
    auto data = load_resource(p, idx);
    zip_entry_open(zip, name.c_str());
    zip_entry_write(zip, data.get_data(), data.get_size());
    zip_entry_close(zip);
    data.free();
    return true;
}

//------------------------------------------------------------

inline int clamp_int(int val, int to) { return val < 0 ? 0 : (val < to ? val : to - 1); }

//------------------------------------------------------------

inline void write_vert(const renderer::mesh_sm::vert &v, obj_writer &w)
{
    w.add_pos(v.pos, nya_math::vec4(v.color[0], v.color[1], v.color[2], v.color[3]) / 255.0f);
    w.add_normal(v.normal);
    w.add_tc(v.tc);
}

//------------------------------------------------------------

std::string write_mesh(nya_memory::tmp_buffer_ref data, std::string folder, std::string name, float scale, zip_t *zip)
{
    renderer::mesh_sm mesh;
    if (!mesh.load(data.get_data(), data.get_size()))
    {
        data.free();
        return "";
    }
    data.free();

    obj_writer w;

    std::set<int> used_tex;

    int group_idx = 0;
    for (auto &g: mesh.groups)
    {
        char mat_name[255];
        sprintf(mat_name, "material%02d", g.tex_idx);

        if (used_tex.find(g.tex_idx) == used_tex.end())
        {
            w.add_material(mat_name, tex_name("tex", g.tex_idx));
            used_tex.insert(g.tex_idx);
        }

        char group_name[255];
        sprintf(group_name, "group%02d", group_idx++);

        w.add_group(group_name, mat_name);

        for (auto &s: g.geometry)
        {
            for (auto &v: s.verts)
                v.pos *= scale;

            //strip to poly

            const int vcount = (int)s.verts.size();

            if (vcount == 3)
            {
                write_vert(s.verts[0], w);
                write_vert(s.verts[2], w);
                write_vert(s.verts[1], w);
                w.add_face(3);
            }
            else if (vcount == 4)
            {
                write_vert(s.verts[0], w);
                write_vert(s.verts[2], w);
                write_vert(s.verts[3], w);
                write_vert(s.verts[1], w);
                w.add_face(4);
            }
            else
            {
                for (int i = 2; i < vcount; ++i)
                {
                    write_vert(s.verts[i], w);
                    if (i & 1)
                    {
                        write_vert(s.verts[i-2], w);
                        write_vert(s.verts[i-1], w);
                    }
                    else
                    {
                        write_vert(s.verts[i-1], w);
                        write_vert(s.verts[i-2], w);
                    }
                    w.add_face(3);
                }
            }
        }
    }

    bool not_transparent = false;
    bool transparent = false;
    for (auto &g: mesh.groups)
    {
        if (g.transparent)
            transparent = true;
        else
            not_transparent = true;
    }

    if (transparent && not_transparent)
        printf("warning: mmixed transparency in mesh %s\n", name.c_str());

    //if (transparent)
    //    assume(!not_transparent);

    if (!not_transparent)
        name.append("_transparent");

    auto vdata = w.get_string(name + ".mtl");
    auto mdata = w.get_mat_string();

    name = folder + name;

    zip_entry_open(zip, (name + ".obj").c_str());
    zip_entry_write(zip, vdata.data(), vdata.length());
    zip_entry_close(zip);

    zip_entry_open(zip, (name + ".mtl").c_str());
    zip_entry_write(zip, mdata.data(), mdata.length());
    zip_entry_close(zip);

    return name + ".obj";
}

//------------------------------------------------------------

bool write_texture(nya_memory::tmp_buffer_ref tex_data, std::string name, zip_t *zip)
{
    if (!tex_data.get_size())
        return false;

    renderer::gim_decoder tex_dec(tex_data.get_data(), tex_data.get_size());
    nya_memory::tmp_buffer_scoped tex(tex_dec.get_required_size() + nya_formats::tga::tga_minimum_header_size);
    const bool decode_result = tex_dec.decode(tex.get_data(nya_formats::tga::tga_minimum_header_size));
    tex_data.free();
    if (!decode_result)
        return false;

    nya_render::bitmap_rgb_to_bgr((uint8_t *)tex.get_data(nya_formats::tga::tga_minimum_header_size), tex_dec.get_width(), tex_dec.get_height(), 4);

    nya_formats::tga tga;
    tga.width = tex_dec.get_width();
    tga.height = tex_dec.get_height();
    tga.channels = nya_formats::tga::bgra;

    tga.encode_header(tex.get_data());

    zip_entry_open(zip, name.c_str());
    zip_entry_write(zip, tex.get_data(), tex.get_size());
    zip_entry_close(zip);

    return true;
}

//------------------------------------------------------------

bool convert_location4(const void *data, size_t size, std::string name, std::string filename)
{
    poc_file p;
    if (!p.open(data, size))
        return false;

    zip_t *zip = zip_open(filename.c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 0);
    if (!zip)
    {
        printf("Unable to save location %s\n", filename.c_str());
        return false;
    }

    //params
    const float scale = 1.0f / 4.0f;
    const int quad_size = (int)(2048 * scale);
    const int quad_frags = 16;
    const int subfrags = 2;
    const int tex_size = 1024;
    const int frag_size = 64;
    const int bord_size = 2;

    std::vector<std::string> mesh_names;

    auto obj_data = load_resource(p, 16);
    poc_file op;
    if (op.open(obj_data.get_data(), obj_data.get_size()))
    {
        mesh_names.resize(op.get_chunks_count());
        for (int i = 0; i < op.get_chunks_count(); ++i)
            mesh_names[i] = write_mesh(load_resource(op, i), "objects/", base_name("object", i), scale, zip);
    }
    obj_data.free();

    auto obj_tex_data = load_resource(p, 17);
    poc_file otp;
    if (otp.open(obj_tex_data.get_data(), obj_tex_data.get_size()))
    {
        auto textures_data = load_resource(otp, 0);
        poc_file tp;
        if (tp.open(textures_data.get_data(), textures_data.get_size()))
            write_texture(load_resource(tp, 0), tex_name("objects/tex", 0), zip);
    }

    std::string info_str = "<!--Open Horizon location-->\n";
    info_str += "<location name=\"" + name + "\" convertor_version=\"" + version + "\" >\n";

    //ToDo

    info_str += "</location>\n\n";

    zip_entry_open(zip, "info.xml");
    zip_entry_write(zip, info_str.c_str(), info_str.size());
    zip_entry_close(zip);

    //ToDo

    zip_close(zip);

    return true;
}

//------------------------------------------------------------

bool convert_location5(const void *data, size_t size, std::string name, std::string filename)
{
    poc_file p;
    if (!p.open(data, size))
        return false;

    zip_t *zip = zip_open(filename.c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 0);
    if (!zip)
    {
        printf("Unable to save location %s\n", filename.c_str());
        return false;
    }

    //params
    const float scale = 1.0f / 4.0f;
    const int quad_size = (int)(2048 * scale);
    const int quad_frags = 16;
    const int subfrags = 2;
    const int tex_size = 1024;
    const int frag_size = 64;
    const int bord_size = 2;

    std::vector<std::string> mesh_names;

    auto obj_data = load_resource(p, 16);
    poc_file op;
    if (op.open(obj_data.get_data(), obj_data.get_size()))
    {
        mesh_names.resize(op.get_chunks_count());
        for (int i = 0; i < op.get_chunks_count(); ++i)
            mesh_names[i] = write_mesh(load_resource(op, i), "objects/", base_name("object", i), scale, zip);
    }
    obj_data.free();

    union color
    {
        unsigned int u;
        struct { unsigned char b, g, r, a; };
    };

    auto obj_tex_data = load_resource(p, 17);
    poc_file otp;
    if (otp.open(obj_tex_data.get_data(), obj_tex_data.get_size()))
    {
        auto pal_data = load_resource(otp, 0);
        if(pal_data.get_data())
        {
            color palette[256];

            renderer::gim_decoder pal_dec(pal_data.get_data(), pal_data.get_size());
            assert(pal_dec.get_required_size() == sizeof(palette));
            pal_dec.decode(palette);
            pal_data.free();

            for (auto &c: palette)
                std::swap(c.r, c.b);

            auto textures_data = load_resource(otp, 1);
            poc_file tp;
            if (tp.open(textures_data.get_data(), textures_data.get_size()))
            {
                for (int i = 0; i < tp.get_chunks_count(); ++i)
                {
                    auto tex_data = load_resource(tp, i);
                    renderer::gim_decoder tex_dec(tex_data.get_data(), tex_data.get_size());
                    nya_memory::tmp_buffer_scoped tex(tex_dec.get_required_size() + nya_formats::tga::tga_minimum_header_size);
                    tex_dec.decode(tex.get_data(nya_formats::tga::tga_minimum_header_size));
                    tex_data.free();

                    color *colors = (color *)tex.get_data(nya_formats::tga::tga_minimum_header_size);
                    for (int i = 0; i < tex_dec.get_width()*tex_dec.get_height(); ++i)
                        colors[i] = palette[colors[i].r];

                    nya_formats::tga tga;
                    tga.width = tex_dec.get_width();
                    tga.height = tex_dec.get_height();
                    tga.channels = nya_formats::tga::bgra;

                    tga.encode_header(tex.get_data());

                    zip_entry_open(zip, tex_name("objects/tex", i).c_str());
                    zip_entry_write(zip, tex.get_data(), tex.get_size());
                    zip_entry_close(zip);
                }
            }
            textures_data.free();
        }
    }
    obj_tex_data.free();

/*
    std::vector<std::string> tree_mesh_names(3);
    for (int i = 0; i < (int)tree_mesh_names.size(); ++i)
    {
        tree_mesh_names[i] = write_mesh(load_resource(p, 28 + i), "trees/", base_name("tree", i), scale, zip);

        //ToDo
    }
*/

    write_entry(p, 11, "sky.sph", zip);

    write_entry(p, 0, "height_offsets.bin", zip);
    write_entry(p, 1, "heights.bin", zip);
    write_entry(p, 5, "tex_offsets.bin", zip);

    const int frag_per_line = tex_size / (frag_size + bord_size * 2);
    const int frag_per_tex = frag_per_line * frag_per_line;

    static_assert(frag_per_tex < 256, "fragments per texture exceeds index type limit");

    auto tc_ind = load_resource(p, 6);
    auto tc_ind_buf = (unsigned char *)tc_ind.get_data();
    for (size_t i = 0; i < tc_ind.get_size(); i += 2)
    {
        const int idx = tc_ind_buf[i] + (tc_ind_buf[i+1] & 3) * 256; //10bit
        tc_ind_buf[i] = idx % frag_per_tex;
        tc_ind_buf[i+1] = idx / frag_per_tex;;
    }

    zip_entry_open(zip, "tex_indices.bin");
    zip_entry_write(zip, tc_ind.get_data(), tc_ind.get_size());
    zip_entry_close(zip);
    tc_ind.free();

    std::string info_str = "<!--Open Horizon location-->\n";
    info_str += "<location name=\"" + name + "\" convertor_version=\"" + version + "\" >\n";

    nya_memory::tmp_buffer_scoped res(load_resource(p, 7));
    nya_memory::tmp_buffer_scoped pal_res(load_resource(p, 8));

    struct tex_header
    {
        uint32_t count;
        uint32_t mip_offsets[4];
        uint32_t padding[3];
    };

    if (res.get_size() >= sizeof(tex_header))
    {
        const auto header = (tex_header *)res.get_data();

        assert(pal_res.get_size() == 256 * 4);
        color *pal = (color *)pal_res.get_data();
        color palette[256];
        for (int i = 0; i < 256; i+=32)
        {
            memcpy(&palette[i], &pal[i], 32);
            memcpy(&palette[i + 16], &pal[i + 8], 32);
            memcpy(&palette[i + 8], &pal[i + 16], 32);
            memcpy(&palette[i + 24], &pal[i + 24], 32);
        }

        for(auto &p: palette)
        {
            std::swap(p.r, p.b);
            p.a = p.a > 127 ? 255 : p.a * 2;
        }

        struct frag { unsigned char data[frag_size][frag_size]; };

        assert(res.get_size() >= header->mip_offsets[0] + sizeof(frag) * header->count);

        nya_formats::tga tga;
        tga.width = tga.height = tex_size;
        tga.channels = nya_formats::tga::bgra;

        const frag *f = (frag *)res.get_data(header->mip_offsets[0]), *last_f = f + header->count;
        const int tiles_count = header->count / frag_per_tex + 1;
        for (int i = 0; i < tiles_count; ++i)
        {
            nya_memory::tmp_buffer_scoped buf(tex_size * tex_size * tga.channels + nya_formats::tga::tga_minimum_header_size);
            memset(buf.get_data(), 0, buf.get_size());

            tga.encode_header(buf.get_data());

            color *tdata = (color *)buf.get_data(nya_formats::tga::tga_minimum_header_size);
            for (int y = 0; y < frag_per_line && f < last_f; ++y)
            {
                color *lv = tdata + tex_size * ((frag_size + bord_size * 2) * y + bord_size);
                for (int x = 0; x < frag_per_line && f < last_f; ++x, ++f)
                {
                    color *lh = lv + x * (frag_size + bord_size * 2);
                    for (int dy = -bord_size; dy < frag_size + bord_size; ++dy)
                    {
                        color *ldv = lh + tex_size * dy + bord_size;
                        for (int dx = -bord_size; dx < frag_size + bord_size; ++dx)
                            ldv[dx] = palette[f->data[clamp_int(dy, frag_size)]
                                                     [clamp_int(dx, frag_size)]];
                    }
                }
            }

            zip_entry_open(zip, tex_name("land", i).c_str());
            zip_entry_write(zip, buf.get_data(), buf.get_size());
            zip_entry_close(zip);
        }

        info_str += "\t<tiles tex_count=\"" + std::to_string(tiles_count) + "\" " +
                    "quad_size=\"" + std::to_string(quad_size) + "\" " +
                    "quad_frags=\"" + std::to_string(quad_frags) + "\" " +
                    "subfrags=\"" + std::to_string(subfrags) + "\" " +
                    "frag_size=\"" + std::to_string(frag_size) + "\" " +
                    "frag_border=\"" + std::to_string(bord_size) + "\"/>\n";
    }

    const int height_quad_frags = quad_frags * 2;
    const std::string height_format = "byte";
    const float height_scale = 80.0f * scale; //100.0f

    info_str += "\t<heightmap format=\"" + height_format + "\" " +
                "scale=\"" + std::to_string(height_scale) + "\" " +
                "quad_frags=\"" + std::to_string(height_quad_frags) + "\"/>\n";

    info_str += "</location>\n\n";

    zip_entry_open(zip, "info.xml");
    zip_entry_write(zip, info_str.c_str(), info_str.size());
    zip_entry_close(zip);

    auto obj = load_resource(p, 15);
    const int obj_count = *(int*)obj.get_data();
    std::string objects_str = "<!--Open Horizon location objects-->\n";
    objects_str += "<objects>\n";
    struct obj_struct { nya_math::vec3 pos; int idx; };
    const auto objs = (obj_struct *)obj.get_data(16);
    for (int i = 0; i < obj_count; ++i)
    {
        auto &o = objs[i];
        objects_str += "\t<object x=\"" + std::to_string(o.pos.x * scale) + "\" " +
                                 "y=\"" + std::to_string(o.pos.y * scale) + "\" " +
                                 "z=\"" + std::to_string(o.pos.z * scale) + "\" " +
                                 "file=\"" + mesh_names[o.idx] + "\"/>\n";
    }
    objects_str += "</objects>\n\n";
    obj.free();
    zip_entry_open(zip, "objects.xml");
    zip_entry_write(zip, objects_str.c_str(), objects_str.size());
    zip_entry_close(zip);

    zip_close(zip);

    return true;
}

//------------------------------------------------------------

const size_t dds_header_size = 128;
void write_dds_header(void *data, uint width, uint height, uint mips, bool alpha)
{
    memset(data, 0, dds_header_size);
    uint header[dds_header_size/4] = {' SDD', dds_header_size - 4, 0x000a1007,
        height, width, alpha ? width : width/2, 0, mips};
    header[20] = 0x00000004;
    header[21] = alpha ? '5TXD' : '1TXD';
    header[27] = 0x00401008;
    memcpy(data, header, dds_header_size);
}

//------------------------------------------------------------

//debug
bool write_texture_white(std::string name, zip_t *zip)
{
    nya_formats::tga t;
    t.width = t.height = 4;
    t.channels = t.bgra;
    nya_memory::tmp_buffer_scoped tmp(t.tga_minimum_header_size + t.width*t.height*t.channels);
    memset(tmp.get_data(t.tga_minimum_header_size), 255, tmp.get_size()-t.tga_minimum_header_size);
    t.encode_header(tmp.get_data());
    zip_entry_open(zip, name.c_str());
    zip_entry_write(zip, tmp.get_data(), tmp.get_size());
    zip_entry_close(zip);
    return true;
}

//------------------------------------------------------------

bool write_texture_ntxr(nya_memory::tmp_buffer_ref tex_data, std::string name, zip_t *zip)
{
    //return write_texture_white(name, zip);

    if (!tex_data.get_size())
        return false;

    nya_memory::memory_reader r(tex_data.get_data(), tex_data.get_size());

    r.seek(32);
    auto mip_count = swap_bytes(r.read<uint16_t>());
    auto format = swap_bytes(r.read<uint16_t>());
    auto width = swap_bytes(r.read<uint16_t>());
    auto height = swap_bytes(r.read<uint16_t>());
    r.seek(48);

    if (format != 0 && format != 2)
    {
        printf("Warning: unsupported texture format: %d\n", 2);
        tex_data.free();
        return false;
    }

    const size_t data_offset = swap_bytes(r.read<uint32_t>()) + 16;
    const bool has_alpha = format == 2;
    nya_memory::tmp_buffer_scoped tmp(dds_header_size + tex_data.get_size()-data_offset);

    const uint psize = has_alpha ? 16 : 8;
    auto src = (const char *)tex_data.get_data(data_offset);
    auto dst = (char *)tmp.get_data(dds_header_size);

    for (uint i = 0, w = width, h = height; i < mip_count; ++i, w = w > 4 ? w/2 : 4, h = h > 4 ? h / 2 : 4)
    {
        UntileDXT(src, dst, w, h, has_alpha);
        const uint mip_size = w * h * psize / 16;

        if (w > 64)
            src += mip_size;
        else if (w == 64)
            src += 64 * 64 * 4;
        else if (w == 32)
            src += 64 * 66 * 4;
        else if (w == 16)
            src -= 64 * 4;
        else if (w == 8)
        {
            mip_count = i + 1;
            break;
        }

        dst += mip_size;
    }

    write_dds_header(tmp.get_data(), width, height, mip_count, has_alpha);

    tex_data.free();

    auto *data = (ushort *)tmp.get_data(dds_header_size);
    auto *end = data + (tmp.get_size() - dds_header_size)/2;
    while(data < end)
        *data = swap_bytes(*data), ++data;

    zip_entry_open(zip, name.c_str());
    zip_entry_write(zip, tmp.get_data(), tmp.get_size());
    zip_entry_close(zip);
    return true;
}

//------------------------------------------------------------

unsigned int get_texture_ntxr_hex_id(nya_memory::tmp_buffer_ref tex_data)
{
    if (!tex_data.get_size())
        return 0;

    nya_memory::memory_reader reader(tex_data.get_data(), tex_data.get_size());
    reader.seek(28);
    auto offset = swap_bytes(reader.read<uint16_t>());
    assert(reader.seek(offset) && reader.test("GIDX",4));
    reader.seek(offset+8);
    return swap_bytes(reader.read<uint32_t>());
}

//------------------------------------------------------------

bool write_texture_ntxr_hex_id(nya_memory::tmp_buffer_ref tex_data, std::string name, zip_t *zip)
{
    return write_texture_ntxr(tex_data, name + std::to_string(get_texture_ntxr_hex_id(tex_data)) + ".dds", zip);
}

//------------------------------------------------------------

bool write_color_curve(nya_math::vec3 in_min, nya_math::vec3 in_gamma, nya_math::vec3 in_max, nya_math::vec3 out_min, nya_math::vec3 out_max, zip_t *zip)
{
    nya_formats::tga tga;
    tga.width = 256;
    tga.height = 1;
    tga.channels = nya_formats::tga::bgr;

    nya_memory::tmp_buffer_scoped tex(256*3 + nya_formats::tga::tga_minimum_header_size);
    tga.encode_header(tex.get_data());

    nya_math::vec3 colors[256];
    typedef nya_math::vec3 vec3;
    for (int j = 0; j < 256; ++j)
    {
        const float fc = j / 255.0f;
        const vec3 in_c(fc, fc, fc);
        vec3 &out_c = colors[j];

        out_c = vec3::min(vec3::max(in_c - vec3(in_min), vec3()) / (vec3(in_max) - vec3(in_min)), vec3(1.0f, 1.0f, 1.0f));

        out_c.x = powf(out_c.x, 1.0f / in_gamma.x);
        out_c.y = powf(out_c.y, 1.0f / in_gamma.y);
        out_c.z = powf(out_c.z, 1.0f / in_gamma.z);

        out_c = out_min + (out_max - out_min) * out_c;
    }

    uint8_t *ucolors = (uint8_t *)tex.get_data(nya_formats::tga::tga_minimum_header_size);
    for (auto &c: colors)
    {
        c = c.clamp(nya_math::vec3(0.0, 0.0, 0.0), nya_math::vec3(1.0, 1.0, 1.0)) * 255.0f;
        *ucolors++ = c.z;
        *ucolors++ = c.y;
        *ucolors++ = c.x;
    }

    zip_entry_open(zip, "tonecurve.tga");
    zip_entry_write(zip, tex.get_data(), tex.get_size());
    zip_entry_close(zip);
    return true;
}

//------------------------------------------------------------

std::string write_mesh_ndxr(nya_memory::tmp_buffer_ref data, std::string folder, const std::vector<unsigned int> &location_tex_hashes, std::string name, zip_t *zip)
{
    renderer::mesh_ndxr mesh;
    if(!mesh.load(data.get_data(), data.get_size(), nya_render::skeleton(), true))
    {
        data.free();
        return "";
    }
    data.free();

    assert(mesh.indices4b.empty()); //ToDo

    if (mesh.groups.empty())
        return "";

    //ToDo: ac6 stores all mesh instances geometry in one file. Extract rotations and colors like it's done in acah to support instanced drawing

    obj_writer w;

    for (auto &v: mesh.verts)
    {
        w.add_pos(v.pos, nya_math::vec4(v.color[0], v.color[1], v.color[2], v.color[3]) / 255.0f); //ToDo
        w.add_normal(v.get_normal());
        w.add_tc(v.tc);
    }

    for (auto &g: mesh.groups)
    {
        int rg_idx = 0;
        for (auto &rg: g.rgroups)
        {
            assume(!rg.textures.empty());

            auto mat_name = base_name(g.name + "_material", rg_idx++);

            std::string tex;

            auto loc_hash = std::find(location_tex_hashes.begin(), location_tex_hashes.end(), rg.textures[0]);
            if (loc_hash != location_tex_hashes.end())
                tex = tex_name("../land", int(loc_hash - location_tex_hashes.begin()), "dds");
            else
                tex = tex_name("tex", rg.textures[0]);

            w.add_material(mat_name, tex);

            w.add_group(g.name, mat_name);

            const uint16_t primitive_restart = uint16_t(-1);

            for (int i = rg.offset + 2, flip = 0; i < rg.offset + rg.count; ++i)
            {
                if (mesh.indices2b[i] == primitive_restart)
                {
                    i += 2;
                    flip = 0;
                    continue;
                }

                w.add_face(mesh.indices2b[i],mesh.indices2b[i-2+flip],mesh.indices2b[i-1-flip]);
                flip = 1 - flip;
            }
        }
    }

    bool transparent = false;
    //ToDo
    if (transparent)
        name.append("_transparent");

    auto vdata = w.get_string(name + ".mtl");
    auto mdata = w.get_mat_string();

    name = folder + name;

    zip_entry_open(zip, (name + ".obj").c_str());
    zip_entry_write(zip, vdata.data(), vdata.length());
    zip_entry_close(zip);

    zip_entry_open(zip, (name + ".mtl").c_str());
    zip_entry_write(zip, mdata.data(), mdata.length());
    zip_entry_close(zip);

    return name + ".obj";
}

//------------------------------------------------------------

bool convert_location6(const void *data, size_t size, std::string name, std::string filename)
{
    struct data_adaptor: public nya_resources::resource_data
    {
        const void *data;
        size_t size;
        data_adaptor(const void *d, size_t s): data(d), size(s) {}
        size_t get_size() override { return size; }
        bool read_chunk(void *d,size_t size,size_t offset=0) override
        {
            return d && (offset + size <= this->size) && memcpy(d, (char *)data+offset, size) != 0;
        }
    } da(data, size);

    fhm_file p;
    if (!p.open(&da))
        return false;

    zip_t *zip = zip_open(filename.c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 0);
    if (!zip)
    {
        printf("Unable to save location %s\n", filename.c_str());
        return false;
    }

    auto &r = p.get_root();
    if (r.folders.size() < 2)
        return false;

    auto &loc_folder = r.folders[0];
    auto &eff_folder = r.folders[1];

    if (loc_folder.files.size() < 11 || loc_folder.folders.size() < 3)
        return false;

    printf("\tterrain textures\n");

    std::vector<unsigned int> location_tex_hashes;
    int tex_count = 0;
    for (auto tidx: loc_folder.folders[2].files)
    {
        auto r = load_resource(p, tidx);
        location_tex_hashes.push_back(get_texture_ntxr_hex_id(r));
        write_texture_ntxr(r, tex_name("land", tex_count++, "dds"), zip); //TEST
    }

    printf("\tobjects\n");

    std::vector<std::string> mesh_names;

    for (auto f: loc_folder.folders[0].files)
        mesh_names.push_back(write_mesh_ndxr(load_resource(p, f), "objects/", location_tex_hashes, base_name("object", int(mesh_names.size())), zip));

    auto obj_pos_data = load_resource(p, loc_folder.files[11]);
    nya_memory::memory_reader obj_pos_reader(obj_pos_data.get_data(), obj_pos_data.get_size());
    std::string objects_str = "<!--Open Horizon location objects-->\n";
    objects_str += "<objects>\n";

    const int location_size = 16;
    for (int pz = 0; pz < location_size; ++pz)
    for (int px = 0; px < location_size; ++px)
    {
        const int idx = pz * location_size + px;
        obj_pos_reader.seek(idx * 16);
        uint32_t count = swap_bytes(obj_pos_reader.read<uint32_t>());
        uint32_t offset = swap_bytes(obj_pos_reader.read<uint32_t>());
        uint32_t unknown = swap_bytes(obj_pos_reader.read<uint32_t>());
        assume(obj_pos_reader.read<uint32_t>() == 0);

        const float base_x = 512.0f * (8 + 16 * (px - location_size/2));
        const float base_z = 512.0f * (8 + 16 * (pz - location_size/2));

        obj_pos_reader.seek(offset);
        for (int i = 0; i < count; ++i)
        {
            nya_math::vec3 pos = obj_pos_reader.read<nya_math::vec3>();
            auto *u = (uint32_t *)&pos;
            for (int j = 0; j < 3; ++j)
                u[j] = swap_bytes(u[j]);

            pos.x += base_x;
            pos.z += base_z;

            auto unknown = obj_pos_reader.read<uint8_t>();
            auto model_idx = obj_pos_reader.read<uint8_t>();
            auto group_idx = swap_bytes(obj_pos_reader.read<uint16_t>());

            objects_str += "\t<object x=\"" + std::to_string(pos.x) + "\" " +
            "y=\"" + std::to_string(pos.y) + "\" " +
            "z=\"" + std::to_string(pos.z) + "\" " +
            "group=\"" + std::to_string(group_idx) + "\" " + //ToDo
            "file=\"" + mesh_names[model_idx] + "\"/>\n";
        }
    }

    objects_str += "</objects>\n\n";
    obj_pos_data.free();
    zip_entry_open(zip, "objects.xml");
    zip_entry_write(zip, objects_str.c_str(), objects_str.size());
    zip_entry_close(zip);

    printf("\tobject textures\n");

    for (auto tidx: loc_folder.folders[1].files)
        write_texture_ntxr_hex_id(load_resource(p, tidx), "objects/tex", zip);

    printf("\tother\n");

    if (eff_folder.files.size() > 2)
        write_entry(p, eff_folder.files[2], "sky.sph", zip);

    if (eff_folder.files.size() > 3)
    {
        pugi::xml_document doc;
        auto buf = load_resource(p, eff_folder.files[3]);
        doc.load_buffer((const char *)buf.get_data(), buf.get_size());
        buf.free();

        auto root = doc.first_child();

        nya_math::vec3 in_min, in_gamma, in_max, out_min, out_max;

        float sat_delta = 0.0f; //ToDo

        for (auto it = root.first_child(); it; it = it.next_sibling())
        {
            std::string name = it.attribute("name").as_string();
            auto v = it.first_child().value();
            float f = atof(v);

            if (name == ".Saturation.Delta") sat_delta = f; //ToDo: / 255.0f; ?

            else if (name == ".LevelCorrection.In.Min.R") in_min.x = f / 255.0f;
            else if (name == ".LevelCorrection.In.Min.G") in_min.y = f / 255.0f;
            else if (name == ".LevelCorrection.In.Min.B") in_min.z = f / 255.0f;

            else if (name == ".LevelCorrection.In.Gamma.R") in_gamma.x = f;
            else if (name == ".LevelCorrection.In.Gamma.G") in_gamma.y = f;
            else if (name == ".LevelCorrection.In.Gamma.B") in_gamma.z = f;

            else if (name == ".LevelCorrection.In.Max.R") in_max.x = f / 255.0f;
            else if (name == ".LevelCorrection.In.Max.G") in_max.y = f / 255.0f;
            else if (name == ".LevelCorrection.In.Max.B") in_max.z = f / 255.0f;

            else if (name == ".LevelCorrection.Out.Min.R") out_min.x = f / 255.0f;
            else if (name == ".LevelCorrection.Out.Min.G") out_min.y = f / 255.0f;
            else if (name == ".LevelCorrection.Out.Min.B") out_min.z = f / 255.0f;

            else if (name == ".LevelCorrection.Out.Max.R") out_max.x = f / 255.0f;
            else if (name == ".LevelCorrection.Out.Max.G") out_max.y = f / 255.0f;
            else if (name == ".LevelCorrection.Out.Max.B") out_max.z = f / 255.0f;
        }
        
        write_color_curve(in_min, in_gamma, in_max, out_min, out_max, zip);
    }

    std::string info_str = "<!--Open Horizon location-->\n";
    info_str += "<location name=\"" + name + "\" convertor_version=\"" + version + "\" >\n";

    if (loc_folder.files.size() > 11)
    {
        write_entry(p, loc_folder.files[9], "tex_offsets.bin", zip);

        auto tc_ind = load_resource(p, loc_folder.files[10]);
        auto utcdata = (uint16_t *)tc_ind.get_data();
        for (int i = 0; i < tc_ind.get_size()/2; ++i, ++utcdata)
            *utcdata = swap_bytes(*utcdata);
        zip_entry_open(zip, "tex_indices.bin");
        zip_entry_write(zip, tc_ind.get_data(), tc_ind.get_size());
        zip_entry_close(zip);
        tc_ind.free();

        info_str += "\t<tiles tex_count=\"" + std::to_string(tex_count) + "\" " +
        "quad_size=\"512\" quad_frags=\"16\" subfrags=\"4\" frag_size=\"256\" frag_border=\"8\"/>\n";

        write_entry(p, loc_folder.files[4], "height_offsets.bin", zip);

        auto hdata = load_resource(p, loc_folder.files[5]);
        auto uhdata = (uint32_t *)hdata.get_data();
        for (int i = 0; i < hdata.get_size()/4; ++i, ++uhdata)
        {
            *uhdata = swap_bytes(*uhdata);
            if (*(float *)uhdata > 9990.0f)
                *(float *)uhdata = -9999.0f; //make consistent with ACAH
        }
        zip_entry_open(zip, "heights.bin");
        zip_entry_write(zip, hdata.get_data(), hdata.get_size());
        zip_entry_close(zip);
        hdata.free();

        info_str += "\t<heightmap format=\"float\" quad_frags=\"8\"/>\n";
    }

    info_str += "</location>\n\n";

    zip_entry_open(zip, "info.xml");
    zip_entry_write(zip, info_str.c_str(), info_str.size());
    zip_entry_close(zip);

    zip_close(zip);

    return true;
}

//------------------------------------------------------------

int main(int argc, const char * argv[])
{
#ifndef _WIN32
    chdir(nya_system::get_app_path());
#endif

    std::string src_path;
    std::string dst_path = "locations/";

    if (argc > 1)
    {
        src_path = argv[1];
        if (!src_path.empty() && src_path.back() != '/')
            src_path.push_back('/');
    }

    if (argc > 2)
    {
        dst_path = argv[2];
        if (!dst_path.empty() && dst_path.back() != '/')
            dst_path.push_back('/');
    }

    create_path(dst_path.c_str());

    cdp_file pak4;
    pac5_file pak5;
    pac6_file pak6;

    std::string ac4_path = src_path + "DATA.CDP";
    std::string ac5_path = src_path + "DATA.PAC";
    std::string ac6_path = src_path + "DATA00.PAC";

/*
    if (file_exists(ac4_path.c_str()) && pak4.open(ac4_path.c_str()))
    {
        for (int i = 1, idx = 0; i <= 100; ++i, ++idx)
        {
            printf("Convertiong AC4 location: %d\n", idx);

            nya_memory::tmp_buffer_scoped b(pak4.get_file_size(i));
            if (!pak4.read_file_data(i, b.get_data()))
                continue;

            convert_location4(b.get_data(), b.get_size(), loc_name("AC4", idx), loc_filename(dst_path + "ac4", idx));
        }

        printf("Done\n");
    }
    else
*/

    if (file_exists(ac5_path.c_str()) && pak5.open(ac5_path.c_str()))
    {
        if (pak5.get_files_count() < 1200) //ac5
        {
            for (int i = 3, idx = 0; i <= 72; ++i, ++idx)
            {
                printf("Converting AC5 location: %d\n", idx);

                nya_memory::tmp_buffer_scoped b(pak5.get_file_size(i));
                if (!pak5.read_file_data(i, b.get_data()))
                    continue;

                convert_location5(b.get_data(), b.get_size(), loc_name("AC5", idx), loc_filename(dst_path + "ac5", idx));
            }
        }
        else //acz
        {
            for (int i = 8, idx = 0; i <= 111; ++i, ++idx)
            {
                printf("Converting ACZ location: %d\n", idx);

                nya_memory::tmp_buffer_scoped b(pak5.get_file_size(i));
                if (!pak5.read_file_data(i, b.get_data()))
                    continue;

                convert_location5(b.get_data(), b.get_size(), loc_name("ACZ", idx), loc_filename(dst_path + "acz", idx));
            }
        }

        printf("Done\n");
    }
    else if (file_exists(ac6_path.c_str()) && pak6.open(ac6_path.c_str()))
    {
        for (int i = 119, idx = 0; i <= 133; ++i, ++idx)
        {
            printf("Converting AC6 location: %d\n", idx);

            nya_memory::tmp_buffer_scoped b(pak6.get_file_size(i));
            if (!pak6.read_file_data(i, b.get_data()))
                continue;

            convert_location6(b.get_data(), b.get_size(), loc_name("AC6", idx), loc_filename(dst_path + "ac6", idx));
        }

        printf("Done\n");
    }
    else
        printf("No data found in src directory.\n");

    return 0;
}

//------------------------------------------------------------
