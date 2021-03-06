/*
 * Copyright 2013 Ole Loots <ole@monochrom.net>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Module Description:
 *
 * AES Object tree tools.
 *
 */

#include <assert.h>
#include "gemtk.h"

char *gemtk_obj_get_text(OBJECT * tree, short idx)
{
    static char p[]="";
    USERBLK *user;
    char *retval;

    switch (tree[idx].ob_type & 0x00FF) {
    case G_BUTTON:
    case G_STRING:
    case G_TITLE:
        return( tree[idx].ob_spec.free_string);
    case G_TEXT:
    case G_BOXTEXT:
    case G_FTEXT:
    case G_FBOXTEXT:
        return (tree[idx].ob_spec.tedinfo->te_ptext);
    case G_ICON:
    case G_CICON:
        return (tree[idx].ob_spec.iconblk->ib_ptext);
        break;

    default:
        break;
    }
    return (p);
}

static void set_text(OBJECT *obj, short idx, char * text, int len)
{
    char spare[255];

    if( len > 254 )
        len = 254;
    if( text != NULL ) {
        strncpy(spare, text, 254);
    } else {
        strcpy(spare, "");
    }

    set_string(obj, idx, spare);
}

char gemtk_obj_set_str_safe(OBJECT * tree, short idx, char *txt)
{
    char spare[204];
    short type = 0;
    short maxlen = 0;
    TEDINFO *ted;


    type = (tree[idx].ob_type & 0xFF);
    if (type == G_FTEXT || type == G_FBOXTEXT) {
        TEDINFO *ted = ((TEDINFO *)get_obspec(tree, idx));
        maxlen = ted->te_txtlen+1;
        if (maxlen > 200) {
            maxlen = 200;
        } else if (maxlen < 0) {
            maxlen = 0;
        }
    } else {
        assert((type == G_FTEXT) || (type == G_FBOXTEXT));
    }

    snprintf(spare, maxlen, "%s", txt);
    set_string(tree, idx, spare);
}

OBJECT *gemtk_obj_get_tree(int idx)
{

    OBJECT *tree;

    rsrc_gaddr(R_TREE, idx, &tree);

    return tree;
}

bool gemtk_obj_is_inside(OBJECT * tree, short obj, GRECT *area)
{
    GRECT obj_screen;
    bool ret = false;

    objc_offset(tree, obj, &obj_screen.g_x, &obj_screen.g_y);
    obj_screen.g_w = tree[obj].ob_width;
    obj_screen.g_h = tree[obj].ob_height;

    ret = RC_WITHIN(&obj_screen, area);

    return(ret);
}

GRECT * gemtk_obj_screen_rect(OBJECT * tree, short obj)
{
    static GRECT obj_screen;

    get_objframe(tree, obj, &obj_screen);

    return(&obj_screen);
}


void gemtk_obj_mouse_sprite(OBJECT *tree, int index)
{
    MFORM mform;
    int dum;

    if ((tree[index].ob_type & 0xFF) != G_ICON)
        return;

    dum = tree[index].ob_spec.iconblk->ib_char;
    mform . mf_nplanes = 1;
    mform . mf_fg = (dum>>8)&0x0F;
    mform . mf_bg = dum>>12;
    mform . mf_xhot = 0; /* to prevent the mform to "jump" on the */
    mform . mf_yhot = 0; /* screen (zebulon rules!) */

    for( dum = 0; dum<16; dum ++) {
        mform . mf_mask[dum] = tree[index].ob_spec.iconblk->ib_pmask[dum];
        mform . mf_data[dum] = tree[index].ob_spec.iconblk->ib_pdata[dum];
    }
    graf_mouse(USER_DEF, &mform);
}


/*
 * gemtk_obj_tree_copy
 *
 * Copy a complete object-tree including all substructures (optional).
 *
 * CAUTION: The object-tree *must* have the LASTOB-flag (0x20) set in
 * it's physically last member.
 *
 * BUG: Up to now tree_copy won't copy the color-icon-structure,
 * because I'm too lazy ;) Maybe I'll do that one day. If you need it
 * urgently, contact me and force me to work... Btw, this doesn't mean
 * that G_CICONs won't be copied at all, but the copied tree will
 * share the CICONBLKs with the original.
 *
 * Input:
 * tree: Pointer to tree which should be copied
 * what: Specifies what substructures should be copied, too (see the
 *       C_xxx-definitions in tree-copy.h for details)
 *
 * Output:
 * NULL: Tree couldn't be copied (due to lack of memory)
 * otherwise: Pointer to copied tree, use free to dealloc it's memory
 */
OBJECT *gemtk_obj_tree_copy(OBJECT *tree)
{
    int16_t	i, objects;
    size_t	to_malloc, size;
    OBJECT	*new_tree;
    char	*area;

    /* Calculate the number of bytes we need for the new tree */
    to_malloc = (size_t) 0;
    for (i = 0;;) {

        /* Size of the OBJECT-structure itself */
        to_malloc += sizeof(OBJECT);

        switch (tree[i].ob_type & 0xff)	{
        case G_TEXT:
        case G_BOXTEXT:
        case G_FTEXT:
        case G_FBOXTEXT:
            /* Size of a TEDINFO-structure */
            to_malloc += sizeof(TEDINFO);

            /* Sizes of the strings in the TEDINFO-structure */
            to_malloc += (size_t)tree[i].ob_spec.tedinfo->te_txtlen;
            to_malloc += (size_t)tree[i].ob_spec.tedinfo->te_txtlen;
            to_malloc += (size_t)tree[i].ob_spec.tedinfo->te_tmplen;
            break;

        case G_IMAGE:

			/* Size of the BITBLK-structure */
			to_malloc += sizeof(BITBLK);

			/* Size of the image-data in the BITBLK-structure */
			to_malloc += (size_t)((int32_t)tree[i].ob_spec.bitblk->bi_wb *
                    (int32_t)tree[i].ob_spec.bitblk->bi_hl);

            break;

        case G_USERDEF:
                /* Size of the USERBLK-structure */
                to_malloc += sizeof(USERBLK);
            break;

        case G_BUTTON:
        case G_STRING:
        case G_TITLE:
			/* Size of the string (with one null character at the end) */
			to_malloc += strlen(tree[i].ob_spec.free_string) + 1L;
            break;

        case G_ICON:
			/* Size of the ICONBLK-structure */
            to_malloc += sizeof(BITBLK);

            /* Sizes of icon-data, icon-mask and icon-text */
            to_malloc += (size_t)((int32_t)tree[i].ob_spec.iconblk->ib_wicon *
									(int32_t)tree[i].ob_spec.iconblk->ib_hicon /
									4L + 1L +
									(int32_t)strlen(tree[i].ob_spec.iconblk->ib_ptext));

            break;
        }

        /* If the size is odd, make it even */
        if ((long)to_malloc & 1)
            to_malloc++;

        /* Exit if we've reached the last object in the tree */
        if (tree[i].ob_flags & OF_LASTOB)
            break;

        i++;
    }

    objects = i + 1;

    /* If there's not enough memory left for the new tree, return NULL */
    if ((new_tree = (OBJECT *)calloc(1, to_malloc)) == NULL) {
        return(NULL);
    }

    /*
     * area contains a pointer to the area where we copy the structures to
     */
    area = (char *)((int32_t)new_tree+(int32_t)objects*(int32_t)sizeof(OBJECT));

    for (i = 0; i < objects; i++) {

        /* Copy the contents of the OBJECT-structure */
        new_tree[i] = tree[i];

        /* This was added to assure true copies of the object type */
        new_tree[i].ob_type = tree[i].ob_type;

        switch (tree[i].ob_type & 0xff) {
        case G_TEXT:
        case G_BOXTEXT:
        case G_FTEXT:
        case G_FBOXTEXT:

			/* Copy the contents of the TEDINFO-structure */
			*(TEDINFO *)area = *tree[i].ob_spec.tedinfo;
			new_tree[i].ob_spec.tedinfo = (TEDINFO *)area;
			area += sizeof(TEDINFO);

			/* Copy the strings in the TEDINFO-structure */
			strncpy(area, tree[i].ob_spec.tedinfo->te_ptext,
					tree[i].ob_spec.tedinfo->te_txtlen);
			new_tree[i].ob_spec.tedinfo->te_ptext = area;
			area += tree[i].ob_spec.tedinfo->te_txtlen;
			strncpy(area, tree[i].ob_spec.tedinfo->te_ptmplt,
					tree[i].ob_spec.tedinfo->te_tmplen);
			new_tree[i].ob_spec.tedinfo->te_ptmplt = area;
			area += tree[i].ob_spec.tedinfo->te_tmplen;
			strncpy(area, tree[i].ob_spec.tedinfo->te_pvalid,
					tree[i].ob_spec.tedinfo->te_txtlen);
			new_tree[i].ob_spec.tedinfo->te_pvalid = area;
			area += tree[i].ob_spec.tedinfo->te_txtlen;

            break;

        case G_IMAGE:

			/* Copy the contents of the BITBLK-structure */
			*(BITBLK *)area = *tree[i].ob_spec.bitblk;
			new_tree[i].ob_spec.bitblk = (BITBLK *)area;
			area += sizeof(BITBLK);

			/* Copy the image-data */
			size = (size_t)((int32_t)tree[i].ob_spec.bitblk->bi_wb *
							(int32_t)tree[i].ob_spec.bitblk->bi_hl);
			memcpy(area, tree[i].ob_spec.bitblk->bi_pdata, size);
			new_tree[i].ob_spec.bitblk->bi_pdata = (int16_t *)area;
			area += size;

            break;

        case G_USERDEF:

			/* Copy the contents of the USERBLK-structure */
			*(USERBLK *)area = *tree[i].ob_spec.userblk;
			new_tree[i].ob_spec.userblk = (USERBLK *)area;
			area += sizeof(USERBLK);

            break;

        case G_BUTTON:
        case G_STRING:
        case G_TITLE:

			/* Copy the string */
			size = strlen(tree[i].ob_spec.free_string) + 1L;
			strcpy(area, tree[i].ob_spec.free_string);
			new_tree[i].ob_spec.free_string = area;
			area += size;

            break;

        case G_ICON:

			/* Copy the contents of the ICONBLK-structure */
			*(ICONBLK *)area = *tree[i].ob_spec.iconblk;
			new_tree[i].ob_spec.iconblk = (ICONBLK *)area;
			area += sizeof(ICONBLK);

			size = (size_t)((int32_t)tree[i].ob_spec.iconblk->ib_wicon *
							(int32_t)tree[i].ob_spec.iconblk->ib_hicon /
							8L);
			/* Copy the mask-data */
			memcpy(area, tree[i].ob_spec.iconblk->ib_pmask, size);
			new_tree[i].ob_spec.iconblk->ib_pmask =	(int16_t *)area;
			area += size;

			/* Copy the icon-data */
			memcpy(area, tree[i].ob_spec.iconblk->ib_pdata, size);
			new_tree[i].ob_spec.iconblk->ib_pdata = (int16_t *)area;
			area += size;
			size = strlen(tree[i].ob_spec.iconblk->ib_ptext) + 1L;

			/* Copy the icon-string */
			strcpy(area, tree[i].ob_spec.iconblk->ib_ptext);
			new_tree[i].ob_spec.iconblk->ib_ptext = area;
			area += size;

            break;
        }

        /* Assure that area contains an even address */
        if ((int32_t)area & 1)
            area++;
    }

    return(new_tree);
}
