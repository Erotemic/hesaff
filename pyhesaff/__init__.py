__version__ = '2.3.0'
__author__ = 'Jon Crall'
__author_email__ = 'erotemic@gmail.com'
__url__ = 'https://github.com/Erotemic/pyhesaff'


from pyhesaff import _pyhesaff
from pyhesaff._pyhesaff import (
    DESC_DIM,
    HESAFF_PARAM_DICT,
    HESAFF_TYPED_PARAMS,
    KPTS_DIM,
    adapt_scale,
    alloc_kpts,
    alloc_patches,
    alloc_vecs,
    argparse_hesaff_params,
    detect_feats,
    detect_feats2,
    detect_feats_in_image,
    detect_feats_list,
    detect_num_feats_in_image,
    extract_desc_from_patches,
    extract_patches,
    extract_vecs,
    get_cpp_version,
    get_hesaff_default_params,
    get_is_debug_mode,
    hesaff_kwargs_docstr_block,
    img32_dtype,
    test_rot_invar,
    vtool_adapt_rotation,
)

__all__ = [
    # modules
    '_pyhesaff',
    # constants
    'DESC_DIM',
    'HESAFF_PARAM_DICT',
    'HESAFF_TYPED_PARAMS',
    'KPTS_DIM',
    # functions
    'adapt_scale',
    'alloc_kpts',
    'alloc_patches',
    'alloc_vecs',
    'argparse_hesaff_params',
    'detect_feats',
    'detect_feats2',
    'detect_feats_in_image',
    'detect_feats_list',
    'detect_num_feats_in_image',
    'extract_desc_from_patches',
    'extract_patches',
    'extract_vecs',
    'get_cpp_version',
    'get_hesaff_default_params',
    'get_is_debug_mode',
    'hesaff_kwargs_docstr_block',
    'img32_dtype',
    'test_rot_invar',
    'vtool_adapt_rotation',
]
