FILESEXTRAPATHS_prepend := "${LAYER_PATH_meta-intel-rdk}/downloads:${THISDIR}/frags:"

RDK_KLM_ARCHIVE ?= "file://rdk_klm_src.tar.xz"

SRC_URI_append_axxiax86-64 = " ${RDK_KLM_ARCHIVE} \
			       file://rdk-modules.scc "

BB_STRICT_CHECKSUM_axxiax86-64 = "0"

# Ignore RDK kernel option warnings
KCONF_BSP_AUDIT_LEVEL = "0"

DEPENDS_append = " rsync-native"

do_patch_rdk_modules () {
    STAGING_PATH="drivers/staging/intel"

    # Get release tag from the archive name
    case ${RDK_KLM_ARCHIVE} in
        file*)
            RELEASE_TAG="$(echo ${LAYER_PATH_meta-intel-rdk}/downloads/${RDK_KLM_ARCHIVE} | sed 's/file:\/\///g')"
            [ -L "${RELEASE_TAG}" ] && RELEASE_TAG="$(readlink ${RELEASE_TAG})"
            RELEASE_TAG="$(basename ${RELEASE_TAG})";;
	http*)
            RELEASE_TAG="$(echo ${RDK_KLM_ARCHIVE} | rev | cut -d'/' -f1 | rev)";;
    esac
    RELEASE_TAG="$(echo ${RELEASE_TAG} | cut -d'.' -f1 | sed 's/rdk_klm_src//g')"
    [ -z "${RELEASE_TAG}" ] && RELEASE_TAG="unknown_release_info" \
                            || RELEASE_TAG="${RELEASE_TAG#?}"

    # If no version for KLM modules is set in local.conf, use RELEASE_TAG
    [ -z "${RDK_KLM_VERSION}" -a -n "${RELEASE_TAG}" ] && RDK_KLM_VERSION="${RELEASE_TAG}"

    STAGING_PATH="${STAGING_PATH}/${RDK_KLM_VERSION}"

    cd ${STAGING_KERNEL_DIR}

    # Stash unstaged changes and apply them after
    [ ! -z "$(git status --porcelain)" ] && { git stash; CHANGES=yes; }

    # Create tag before patching the kernel with RDK KLM modules
    git tag before_rdk_commits

    # Create KLM drivers path and add entry to the parent directory Makefile and Kconfig
    if [ -f ${WORKDIR}/rdk/klm/scripts/patch_kernel_tree.sh ]; then
       # Create parent staging path (default is drivers/staging/intel)
       ${WORKDIR}/rdk/klm/scripts/patch_kernel_tree.sh ${STAGING_KERNEL_DIR}
       > ${STAGING_KERNEL_DIR}/$(dirname ${STAGING_PATH})/Kconfig
       > ${STAGING_KERNEL_DIR}/$(dirname ${STAGING_PATH})/Makefile

       # Create version specific path (STAGING_PATH)
       ${WORKDIR}/rdk/klm/scripts/patch_kernel_tree.sh \
          ${STAGING_KERNEL_DIR} ${STAGING_PATH}
       rm -f sed.tmp

       # Add entry to the parent directory Kconfig (patch_kernel_tree.sh doesn't do this)
       echo "source \"${STAGING_PATH}/Kconfig\"" >> \
          ${STAGING_KERNEL_DIR}/$(dirname ${STAGING_PATH})/Kconfig
    fi

    # Install each driver in the specific path and create commits
    for KLMC in cpk cpk-ae hqm ies qat netd
    do
	[ ! -d ${WORKDIR}/rdk/klm/${KLMC} ] && continue

        if [ "hqm" = "${KLMC}" ]; then
	   ${WORKDIR}/rdk/klm/${KLMC}/${KLMC}/scripts/patch_kernel_tree.sh \
              ${STAGING_KERNEL_DIR} ${STAGING_PATH}
        else
	   ${WORKDIR}/rdk/klm/${KLMC}/scripts/patch_kernel_tree.sh \
              ${STAGING_KERNEL_DIR} ${STAGING_PATH}
        fi

        # Handle drivers name differences (cpk->ice_sw; cpk-ae->ice_sw_ae; netd->adk_netd)
        case ${KLMC} in
           "cpk")    KLM_DRIVER="ice_sw";;
           "cpk-ae") KLM_DRIVER="ice_sw_ae";;
           "netd")   KLM_DRIVER="adk_netd";;
           *)        KLM_DRIVER="${KLMC}";;
        esac

        # Add $RDK_KLM_VERSION value in all paths from $KLMC Makefiles
        find ${STAGING_PATH}/${KLM_DRIVER} -name "Makefile" -exec \
           sed -i s#$(dirname ${STAGING_PATH})#${STAGING_PATH}#g {} +

        # Create git commit for ${KLMC} module
	git add -A
	if [ "qat" = "${KLMC}" ]; then
	    # Add the -asn1 files to the commit.
	    if [ -d ${STAGING_PATH}/qat/qat_common ]; then
	       git add -f ${STAGING_PATH}/qat/qat_common/*-asn1.[ch]
	    fi
	fi

        TIPC="$(echo ${KLMC} | tr '[:lower:]' '[:upper:]') ${RDK_KLM_VERSION}"
        [ "${RDK_KLM_VERSION}" != "${RELEASE_TAG}" ] && TIPC="${TIPC} ${RELEASE_TAG}"
	git commit --no-gpg-sign --author="Intel AXXIA <axxia@intel.com>" \
		   --message "${TIPC}" --message "NOT FOR UPSTREAMING"
    done

    # Create tag after patches with RDK KLM modules were created
    git tag after_rdk_commits

    # Apply stashed changes if any
    [ -n "${CHANGES}" ] && git stash apply || exit 0
}

addtask do_patch_rdk_modules after do_patch before do_kernel_configme
do_kernel_configme[depends] += "${PN}:do_patch_rdk_modules"

do_install_append() {
     install -d ${D}/usr/kernel-headers
     make headers_install INSTALL_HDR_PATH="${D}/usr/kernel-headers"
}

sysroot_stage_all_append() {
    # Install headers only for the virtual/kernel provider
    if [ "${KERNEL_PACKAGE_NAME}" = "kernel" ]; then
        KERNEL_HEADERS="${SYSROOT_DESTDIR}/usr/kernel-headers"
        KLM_HEADERS="${KERNEL_HEADERS}/include/klm"

        sysroot_stage_dir "${D}/usr/kernel-headers" "${KERNEL_HEADERS}"

        install -d ${KLM_HEADERS}
        mv ${KERNEL_HEADERS}/include/linux/adf_cfg*.h ${KLM_HEADERS}

	# hqm gets renamed dlb in newer versions of HQM...
	if [ -e ${KERNEL_HEADERS}/include/linux/hqm_user.h ]
	then
		mv ${KERNEL_HEADERS}/include/linux/hqm_user.h ${KLM_HEADERS}
	fi
	if [ -e ${KERNEL_HEADERS}/include/linux/dlb_user.h ]
	then
		mv ${KERNEL_HEADERS}/include/linux/dlb_user.h ${KLM_HEADERS}
	fi

        mv ${KERNEL_HEADERS}/include/linux/ice_sw_ae*.h ${KLM_HEADERS}
        mv ${KERNEL_HEADERS}/include/linux/adk_netd*.h ${KLM_HEADERS}
    fi
}

PACKAGES += "${PN}-kernel-headers"
FILES_${PN}-kernel-headers = "/usr/kernel-headers"
