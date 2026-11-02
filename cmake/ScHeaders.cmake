# Fetches only the SuperCollider plugin headers for a given release tag.
#
# Cloning the SuperCollider repository just to get ~40 headers costs hundreds of
# megabytes and several minutes; downloading the files individually does not.

set(SUPERBAND_SC_HEADER_FILES
    plugin_interface/FFT_UGens.h
    plugin_interface/Hash.h
    plugin_interface/SCComplex.h
    plugin_interface/SC_BelaScope.h
    plugin_interface/SC_BufGen.h
    plugin_interface/SC_Complex.h
    plugin_interface/SC_Constants.h
    plugin_interface/SC_DemandUnit.h
    plugin_interface/SC_FifoMsg.h
    plugin_interface/SC_Graph.h
    plugin_interface/SC_InlineBinaryOp.h
    plugin_interface/SC_InlineUnaryOp.h
    plugin_interface/SC_InterfaceTable.h
    plugin_interface/SC_Node.h
    plugin_interface/SC_PlugIn.h
    plugin_interface/SC_PlugIn.hpp
    plugin_interface/SC_RGen.h
    plugin_interface/SC_Rate.h
    plugin_interface/SC_SndBuf.h
    plugin_interface/SC_Unit.h
    plugin_interface/SC_Wire.h
    plugin_interface/SC_World.h
    plugin_interface/SC_sndfile_stub.h
    plugin_interface/Unroll.h
    plugin_interface/sc_msg_iter.h
    common/SC_Alloca.h
    common/SC_BoundsMacros.h
    common/SC_Endian.h
    common/SC_Export.h
    common/SC_Reply.h
    common/SC_StringBuffer.h
    common/SC_Types.h
    common/SC_fftlib.h
    common/clz.h
    common/function_attributes.h
    common/wintime.h
    server/ErrorMessage.hpp
    server/SC_OSC_Commands.h
    server/SC_WorldOptions.h
)

function(superband_fetch_sc_headers sc_tag dest_dir out_var)
    set(_base "https://raw.githubusercontent.com/supercollider/supercollider/${sc_tag}/include")
    foreach(_rel IN LISTS SUPERBAND_SC_HEADER_FILES)
        get_filename_component(_dir "${dest_dir}/${_rel}" DIRECTORY)
        file(MAKE_DIRECTORY "${_dir}")
        if(NOT EXISTS "${dest_dir}/${_rel}")
            message(STATUS "Fetching SuperCollider header ${_rel} (${sc_tag})")
            file(DOWNLOAD "${_base}/${_rel}" "${dest_dir}/${_rel}"
                 STATUS _status TLS_VERIFY ON SHOW_PROGRESS OFF)
            list(GET _status 0 _code)
            if(NOT _code EQUAL 0)
                list(GET _status 1 _msg)
                file(REMOVE "${dest_dir}/${_rel}")
                message(FATAL_ERROR
                    "Could not fetch SuperCollider header ${_rel} from ${_base}: ${_msg}\n"
                    "Point -DSC_INCLUDE_DIR at an existing SuperCollider include tree instead.")
            endif()
        endif()
    endforeach()
    set(${out_var} "${dest_dir}" PARENT_SCOPE)
endfunction()
