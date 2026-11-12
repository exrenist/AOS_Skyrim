set_xmakever('3.0.1')
set_project('AOS_Skyrim')
set_version(os.getenv('CLIBDT_PROJECT_VERSION') or '1.0.0')
set_languages('c++23')
add_rules('mode.debug', 'mode.release', 'mode.releasedbg')
set_policy('package.requires_lock', true)

local shared = os.getenv('CLIBDT_COMMONLIB_SOURCE')
includes(path.join(shared, 'xmake.lua'))

target('commonlibsse-ng')
    before_config(function(target)
        local header = target:pcheaderfile('cxx')
        if header then target:pcheaderfile_set('cxx', path.absolute(header)) end
    end)
target_end()

target('AOS_Skyrim')
    add_deps('commonlibsse-ng')
    add_defines('NOMINMAX')

    local runtime = 'se_ae'
    if has_config('skyrim_vr') then runtime = 'vr'
    elseif has_config('skyrim_ae') and not has_config('skyrim_se') then runtime = 'ae'
    elseif has_config('skyrim_se') and not has_config('skyrim_ae') then runtime = 'se' end

    add_rules('commonlibsse-ng.plugin', {
        name = 'AOS_Skyrim', 
        author = 'akisar', 
        description = os.getenv('CLIBDT_PROJECT_DESCRIPTION') or 'Altar of Spellmaking in Skyrim NG', 
        runtime = runtime
    })

    add_files('src/**.cpp')
    add_headerfiles('src/**.h')
    add_includedirs('src')

    local function resolve_shared_library(env_var, dir_pattern)
        local root = os.getenv(env_var)
        if not root or #root == 0 then
            if shared and #shared > 0 then
                local lib_dir = path.directory(path.directory(path.directory(shared)))
                for _, dir in ipairs(os.dirs(path.join(lib_dir, dir_pattern))) do
                    root = dir
                    break
                end
            end
        end

        if root and #root > 0 then
            for _, include in ipairs({
                root,
                path.join(root, 'include'),
                path.join(root, 'source'),
                path.join(root, 'source/include'),
                path.join(root, 'include/CLIBUtil/detail')
            }) do
                if os.isdir(include) then 
                    add_includedirs(include) 
                end
            end
        end
    end

    resolve_shared_library('CLIBDT_CLIBUTIL_SOURCE', '*clib*')
    resolve_shared_library('CLIBDT_XBYAK_SOURCE', '*xbyak*')
target_end()