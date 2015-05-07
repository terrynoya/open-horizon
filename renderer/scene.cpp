//
// open horizon -- undefined_darkness@outlook.com
//

#include "scene.h"
#include "shared.h"
#include <algorithm>

namespace renderer
{

//------------------------------------------------------------

nya_scene::texture load_tonecurve(const char *file_name)
{
    nya_scene::shared_texture t;
    auto res = load_resource(file_name);
    assert(res.get_size() == 256 * 3);

    const unsigned char *data = (const unsigned char *)res.get_data();
    unsigned char color[256 * 3];
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 256; ++j)
            color[j*3+i]=data[j+i*256];
    }

    t.tex.build_texture(color, 256, 1, nya_render::texture::color_rgb);
    t.tex.set_wrap(nya_render::texture::wrap_clamp, nya_render::texture::wrap_clamp);
    res.free();
    
    nya_scene::texture result;
    result.create(t);
    return result;
}

//------------------------------------------------------------

aircraft_ptr scene::add_aircraft(const char *name, int color, bool player)
{
    nya_render::texture::set_default_aniso(2);

    auto a = world::add_aircraft(name, color, player);
    if (!player)
        return a;

    m_cam_fp_off = a->get_bone_pos("ckpp");

    //ToDo: research camera
    auto off = -a->get_camera_offset();
    camera.set_ignore_delta_pos(false);
    camera.reset_delta_pos();
    camera.reset_delta_rot();
    //camera.add_delta_pos(off.x, off.y * 1.5, off.z * 0.5);
    camera.add_delta_pos(off.x, -0.5 + off.y*1.5, off.z*0.7);
    if (strcmp(name, "f22a") == 0 || strcmp(name, "kwmr") == 0 || strcmp(name, "su47") == 0 || strcmp(name, "pkfa") == 0)
        camera.add_delta_pos(0.0, -1.0, 3.0);

    if (strcmp(name, "mr2k") == 0)
        camera.add_delta_pos(0.0, 0.0, 2.0);

    if (strcmp(name, "b01b") == 0)
        camera.add_delta_pos(0.0, -4.0, -2.0);

    if (strcmp(name, "su25") == 0)
        camera.add_delta_pos(0.0, -2.0, 0.0);

    if (strcmp(name, "su34") == 0 || strcmp(name, "f16c") == 0 || strcmp(name, "f15m") == 0
        || strcmp(name, "f02a") == 0 || strcmp(name, "su37") == 0 || strcmp(name, "f35b") == 0)
        camera.add_delta_pos(0.0, -1.0, 0.0);

    camera.set_ignore_delta_pos(m_camera_mode != camera_mode_third);
    //shared::clear_textures(); //ToDo
    return a;
}

//------------------------------------------------------------

void scene::set_location(const char *name)
{
    nya_render::texture::set_default_aniso(2);

    if(!m_curve.is_valid())
    {
        m_curve.create();
        load("postprocess.txt");
        set_texture("color_curve", m_curve);
        set_shader_param("screen_radius", nya_math::vec4(1.185185, 0.5 * 4.0 / 3.0, 0.0, 0.0));
        set_shader_param("damage_frame", nya_math::vec4(0.35, 0.5, 1.0, 0.1));
        m_flare.init(get_texture("main_color"), get_texture("main_depth"));
        m_cockpit_black.load("shaders/cockpit_black.nsh");
        m_cockpit_black_quad.init();
    }

    world::set_location(name);

    for (auto &a: m_aircrafts)
        a->apply_location(m_location_name.c_str(), m_location.get_params());

    m_flare.apply_location(m_location.get_params());

    auto &p = m_location.get_params();
    m_curve.set(load_tonecurve((std::string("Map/tonecurve_") + name + ".tcb").c_str()));
    set_shader_param("bloom_param", nya_math::vec4(p.hdr.bloom_threshold, p.hdr.bloom_offset, p.hdr.bloom_scale, 1.0));
    set_shader_param("saturation", nya_math::vec4(p.tone_saturation * 0.01, 0.0, 0.0, 0.0));
    m_luminance_speed = p.hdr.luminance_speed;
    m_fade_time = 2000;
}

//------------------------------------------------------------

void scene::update(int dt)
{
    if (dt > 50)
        dt = 50;

    world::update(dt);

    if (m_player_aircraft.is_valid())
    {
        camera.set_pos(m_player_aircraft->get_bone_pos(m_camera_mode == camera_mode_third ? "camp" : "ckpp"));
        camera.set_rot(m_player_aircraft->get_rot());
/*
        set_shader_param("damage_frame_color", nya_math::vec4(1.0, 0.0, 0.0235, nya_math::max(1.0 - player_plane.get_hp(), 0.0)));
*/
    }

    set_shader_param("lum_adapt_speed", m_luminance_speed * dt / 1000.0f * nya_math::vec4(1.0, 1.0, 1.0, 1.0));

    if (m_fade_time > 0)
    {
        m_fade_time -= dt;
        if (m_fade_time < 0)
            m_fade_time = 0;

        set_shader_param("fade_color", nya_math::vec4(0.0, 0.0, 0.0, m_fade_time / 2500.0f));
    }

    if (m_help_time > 0)
        m_help_time -= dt;

    m_frame_counter_time += dt;
    ++m_frame_counter;
    if (m_frame_counter_time > 1000)
    {
        m_fps = m_frame_counter;
        m_frame_counter = 0;
        m_frame_counter_time -= 1000;
    }
}

//------------------------------------------------------------

void scene::switch_camera()
{
    switch (m_camera_mode)
    {
        case camera_mode_third: m_camera_mode = camera_mode_cockpit; break;
        case camera_mode_cockpit: m_camera_mode = camera_mode_first; break;
        case camera_mode_first: m_camera_mode = camera_mode_third; break;
    }

    camera.set_ignore_delta_pos(m_camera_mode != camera_mode_third);
    //camera.reset_delta_pos();
    camera.reset_delta_rot();
}

//------------------------------------------------------------

void scene::resize(unsigned int width,unsigned int height)
{
    if (!m_fonts_loaded)
    {
        m_ui.load_fonts("UI/text/menuCommon.acf");
        m_fonts_loaded = true;
    }

    nya_scene::postprocess::resize(width, height);

    if (height)
        camera.set_aspect(height > 0 ? float(width) / height : 1.0f);

    m_ui.resize(width, height);
}

//------------------------------------------------------------

void scene::draw()
{
    nya_scene::postprocess::draw(0);

    const auto green = nya_math::vec4(103,223,144,255)/255.0;
    const auto white = nya_math::vec4(1.0, 1.0, 1.0, 1.0);

    //if (m_help_time > 0)
    //    m_ui.draw_text(L"Press 1-2 to change location, 3-4 to change plane, 5-6 to change paint", "NowGE24", 50, 100, white);

    wchar_t buf[255];
    swprintf(buf, sizeof(buf), L"FPS: %d", m_fps);
    m_ui.draw_text(buf, "NowGE20", m_ui.get_width() - 90, 20, white);

    if(m_loading)
    {
        m_loading = false;
        m_ui.draw_text(L"LOADING", "NowGE24", m_ui.get_width() * 0.5 - 50, m_ui.get_height() * 0.5, white);
    }
    else
    {
        /*
         swprintf(buf, sizeof(buf), L"%d", int(player_plane.get_speed()));
         m_ui.draw_text(L"SPEED", "NowGE20", m_ui.get_width() * 0.35, m_ui.get_height() * 0.5 - 20, green);
         m_ui.draw_text(buf, "NowGE20", m_ui.get_width() * 0.35, m_ui.get_height() * 0.5, green);
         swprintf(buf, sizeof(buf), L"%d", int(player_plane.get_alt()));
         m_ui.draw_text(L"ALT", "NowGE20", m_ui.get_width() * 0.6, m_ui.get_height() * 0.5 - 20, green);
         m_ui.draw_text(buf, "NowGE20", m_ui.get_width() * 0.6, m_ui.get_height() * 0.5, green);
         */
        if(m_paused)
            m_ui.draw_text(L"PAUSED", "NowGE24", m_ui.get_width() * 0.5 - 45, m_ui.get_height() * 0.5, white);
    }

    //m_ui.draw_text(L"This is a test. The quick brown fox jumps over the lazy dog's back 1234567890", "NowGE24", 50, 100, green);
    //m_ui.draw_text(L"テストです。いろはにほへと ちりぬるを わかよたれそ つねならむ うゐのおくやま けふこえて あさきゆめみし ゑひもせす。", "ShinGo18outline", 50, 150, green);
    //m_ui.draw_text(L"ASDFGHJKLasdfghjklQWERTYUIOPqwertyuiopZXCVBNMzxcvbnm\"\'*_", "NowGE24", 50, 200, green);
    
}

//------------------------------------------------------------

void scene::draw_scene(const char *pass, const char *tags)
{
    camera.set_near_far(1.0, 21000.0);

    if (strcmp(tags, "location") == 0)
        m_location.draw();
    else if (strcmp(tags, "aircrafts") == 0)
    {
        for (auto &a:m_aircrafts)
        {
            if (a == m_player_aircraft)
                continue;

            a->draw(0);
        }
    }
    else if (strcmp(tags, "player") == 0)
    {
        if (m_camera_mode == camera_mode_third)
            m_player_aircraft->draw(0);
    }
    else if (strcmp(tags, "cockpit") == 0)
    {
        if (m_camera_mode == camera_mode_cockpit)
        {
            /*
             //move player and camera to 0.0 because floats sucks
             nya_render::clear(false, true);
             auto pos = player_plane.get_pos();
             auto cam_pos = camera.get_pos();
             player_plane.set_pos(nya_math::vec3());
             camera.set_pos(player_plane.get_rot().rotate(m_cam_fp_off));
             camera.set_near_far(0.01,10.0);
             //player_plane.draw(2); //ToDo
             player_plane.draw(1);

             //fill holes
             nya_render::set_state(nya_render::state());
             nya_render::depth_test::enable(nya_render::depth_test::not_greater);
             m_cockpit_black.internal().set();
             m_cockpit_black_quad.draw();
             m_cockpit_black.internal().unset();

             //restore
             player_plane.set_pos(pos);
             camera.set_pos(cam_pos);
             */
        }
    }
    else if (strcmp(tags, "clouds_flat") == 0)
        m_clouds.draw_flat();
    else if (strcmp(tags, "clouds_obj") == 0)
        m_clouds.draw_obj();
    else if (strcmp(tags, "flare") == 0)
        m_flare.draw();
}

//------------------------------------------------------------
}