#include  <internal_volume_io.h>
#include  <bicpl.h>

#define  N_OUTPUT  4

int   main(
    int   argc,
    char  *argv[] )
{
    char                  *output_prefix;
    char                  **input_filenames, *input_filename, *example_filename;
    char                  *dim_names_2d[3], *dim_names_3d[3];
    BOOLEAN               this_is_tag_file, tag_files_present;
    int                   n_input_files, d, i, vol, x_dim, max_voxel;
    int                   sizes_3d[3], sizes_2d[3];
    int                   v[5], n_slices, n_chunk_dims;
    int                   slice, s;
    int                   n_tag_volumes, n_tag_points, n_tag_files;
    int                   v0, v1, v2, v3, v4, offset, unique_index;
    nc_type               nc_data_type;          
    Volume                example_volume;
    Real                  **tags1, **tags2, voxel_pos[N_DIMENSIONS];
    Real                  fraction_done, this_value, opposite_value;
    STRING                file_dim_names[N_DIMENSIONS];
    STRING                output_filename;
    char                  *suffixes[] = { "not_left_not_right",
                                          "not_left_right",
                                          "left_not_right",
                                          "left_right" };
    volume_input_struct   input_info;
    Real                  voxel;
    Volume                input_volume, output_volumes[N_OUTPUT], used_volume;
    Minc_file             output_files[N_OUTPUT];
    General_transform     voxel_to_world_transform;
    minc_output_options   output_options;
    progress_struct       progress;

    initialize_argument_processing( argc, argv );

    if( !get_string_argument( "", &output_prefix ) )
    {
        print_error( "Usage: %s output_prefix input1.mnc input2.mnc ...\n",
                     argv[0] );
        print_error( "                -- OR ---\n" );
        print_error(
            "Usage: %s output_prefix example.mnc input1.tag input2.tag ...\n",
            argv[0] );
        return( 1 );
    }

    n_input_files = 0;

    example_filename = NULL;
    tag_files_present = FALSE;
    input_filenames = NULL;
    n_tag_files = 0;

    while( get_string_argument( "", &input_filename ) )
    {
        this_is_tag_file = filename_extension_matches( input_filename,
                                        get_default_tag_file_suffix() );

        if( this_is_tag_file )
            ++n_tag_files;

        if( this_is_tag_file && example_filename == NULL )
        {
            if( n_input_files == 0 )
            {
                print_error(
                        "Must specify an example volume before tag file.\n" );
                return( 1 );
            }

            example_filename = input_filenames[n_input_files-1];
            input_filenames[n_input_files-1] = input_filename;
            tag_files_present = TRUE;
        }
        else
        {
            ADD_ELEMENT_TO_ARRAY( input_filenames, n_input_files,
                                  input_filename, DEFAULT_CHUNK_SIZE );
        }
    }

    if( n_input_files == 0 )
    {
        print_error( "Need at least one input file.\n" );
        return( 1 );
    }

    if( (n_tag_files % 2) != 0 )
    {
        print_error( "Must be an even number of tag files.\n" );
        return( 1 );
    }

    max_voxel = n_input_files - n_tag_files / 2;

    print( "Number of files   : %4d\n", n_input_files );
    print( "Number of subjects: %4d\n", max_voxel );

    if( max_voxel <= 255 )
        nc_data_type = NC_BYTE;
    else
        nc_data_type = NC_SHORT;

    if( example_filename == NULL )
        example_filename = input_filenames[0];

    if( tag_files_present )
        n_chunk_dims = 3;
    else
        n_chunk_dims = 2;

    if( get_file_dimension_names( example_filename, N_DIMENSIONS,
                                  file_dim_names ) != OK )
    {
        print_error( "Error reading dimension names from %s.\n",
                     example_filename );
        return( 1 );
    }

    for_less( d, 0, n_chunk_dims )
        dim_names_2d[d] = file_dim_names[d+N_DIMENSIONS-n_chunk_dims];

    if( n_chunk_dims == 2 && strcmp( file_dim_names[0], MIxspace ) == 0 )
        dim_names_2d[0] = file_dim_names[0];

    for_less( x_dim, 0, n_chunk_dims )
    {
        if( strcmp( file_dim_names[x_dim], MIxspace ) == 0 )
            break;
    }

    /*--- get the 3d information about the file */

    for_less( d, 0, N_DIMENSIONS )
        dim_names_3d[d] = file_dim_names[d];

    if( start_volume_input( example_filename, N_DIMENSIONS, dim_names_3d,
                            NC_UNSPECIFIED, FALSE, 0.0, 0.0, TRUE,
                            &example_volume, NULL, &input_info ) != OK )
    {
        return( 1 );
    }

    get_volume_sizes( example_volume, sizes_3d );
    copy_general_transform( get_voxel_to_world_transform(example_volume),
                            &voxel_to_world_transform );

    delete_volume_input( &input_info );

    if( tag_files_present )
    {
        alloc_volume_data( example_volume );
        BEGIN_ALL_VOXELS( example_volume, v0, v1, v2, v3, v4)
            set_volume_voxel_value( example_volume, v0, v1, v2, v3, v4, 0.0 );
        END_ALL_VOXELS
    }
    else
        delete_volume( example_volume );

    /*--- get the 2d information */

    if( start_volume_input( example_filename, n_chunk_dims, dim_names_2d,
                            NC_UNSPECIFIED, FALSE, 0.0, 0.0, TRUE,
                            &input_volume, NULL, &input_info ) != OK )
    {
        return( 1 );
    }

    n_slices = get_n_input_volumes( input_info.minc_file );

    delete_volume_input( &input_info );

    for_less( i, 0, n_chunk_dims )
    {
        for_less( d, 0, N_DIMENSIONS )
            if( strcmp( dim_names_2d[i], file_dim_names[d] ) == 0 )
                break;
        sizes_2d[i] = sizes_3d[d];
    }

    set_default_minc_output_options( &output_options );
    set_minc_output_real_range( &output_options, 0.0, 100.0 );

    for_less( i, 0, N_OUTPUT )
    {
        print( "Creating volume %d of %d.\n", i+1, N_OUTPUT );

        output_volumes[i] = create_volume( n_chunk_dims, dim_names_2d,
                                           nc_data_type, FALSE,
                                           0.0, (Real) max_voxel );
        set_volume_real_range( output_volumes[i], 0.0, 100.0 );
        set_volume_sizes( output_volumes[i], sizes_2d );
        alloc_volume_data( output_volumes[i] );

        BEGIN_ALL_VOXELS( output_volumes[i], v0, v1, v2, v3, v4)
            set_volume_voxel_value( output_volumes[i],
                                    v0, v1, v2, v3, v4, 0.0 );
        END_ALL_VOXELS

        (void) sprintf( output_filename, "%s_%s.mnc", output_prefix,
                        suffixes[i] );

        output_files[i] = initialize_minc_output( output_filename, N_DIMENSIONS,
                                              file_dim_names, sizes_3d,
                                              nc_data_type, FALSE,
                                              0.0, (Real) max_voxel,
                                              &voxel_to_world_transform,
                                              output_volumes[i],
                                              &output_options );

        if( output_files[i] == NULL )
            return( 1 );

        if( copy_auxiliary_data_from_minc_file( output_files[i],
                                                example_filename, "" ) != OK )
            return( 1 );
    }

    if( n_chunk_dims == 2 )
    {
        initialize_progress_report( &progress, FALSE, n_slices,
                                    "Computing Slices" );
    }
    else
    {
        initialize_progress_report( &progress, FALSE, n_input_files,
                                    "Processing Files" );
    }

    for_less( slice, 0, n_slices )
    {
        vol = 0;
        unique_index = 1;
        while( vol < n_input_files )
        {
            this_is_tag_file = filename_extension_matches( input_filenames[vol],
                                         get_default_tag_file_suffix() );

            if( this_is_tag_file )
            {
                for_less( offset, 0, 2 )
                {
                    if( vol >= n_input_files )
                    {
                        print_error( "Tag files must be in pairs.\n" );
                        return( 1 );
                    }

                    if( input_tag_file( input_filenames[vol], &n_tag_volumes,
                                        &n_tag_points, &tags1, &tags2,
                                        NULL, NULL, NULL, NULL ) != OK )
                        return( 1 );

                    for_less( i, 0, n_tag_points )
                    {
                        convert_world_to_voxel( example_volume,
                                                tags1[i][X],
                                                tags1[i][Y],
                                                tags1[i][Z],
                                                voxel_pos );

                        convert_real_to_int_voxel( N_DIMENSIONS, voxel_pos, v );
                        if( int_voxel_is_within_volume( example_volume, v ))
                        {
                            set_volume_voxel_value( example_volume,
                                            v[0], v[1], v[2], v[3], v[4],
                                            unique_index );
                        }
                    }

                    free_tag_points( n_tag_volumes, n_tag_points, tags1, tags2,
                                     NULL, NULL, NULL, NULL );
                    ++vol;
                }

                used_volume = example_volume;
                ++unique_index;
            }
            else
            {
                if( start_volume_input( input_filenames[vol],
                                        n_chunk_dims, dim_names_2d,
                                        NC_UNSPECIFIED, FALSE, 0.0, 0.0, FALSE,
                                        &input_volume, NULL, &input_info ) !=OK)
                {
                    return( 1 );
                }

                for_less( s, 0, slice )
                    (void) advance_input_volume( input_info.minc_file );

                while( input_more_of_volume( input_volume, &input_info,
                                             &fraction_done ) )
                {
                }

                delete_volume_input( &input_info );

                used_volume = input_volume;

                ++vol;
            }

            BEGIN_ALL_VOXELS( output_volumes[0], v[0], v[1], v[2],v[3],v[4])
                if( this_is_tag_file )
                {
                    this_value = get_volume_voxel_value( used_volume,
                           v[0], v[1], v[2], v[3], v[4]) == unique_index;
                }
                else
                {
                    this_value = get_volume_real_value( used_volume,
                                            v[0], v[1], v[2], v[3], v[4]);
                }

                v[x_dim] = sizes_2d[x_dim] - 1 - v[x_dim];

                if( this_is_tag_file )
                {
                    opposite_value = get_volume_voxel_value( used_volume,
                           v[0], v[1], v[2], v[3], v[4]) == unique_index;
                }
                else
                {
                    opposite_value = get_volume_real_value( used_volume,
                                            v[0], v[1], v[2], v[3], v[4]);
                }

                v[x_dim] = sizes_2d[x_dim] - 1 - v[x_dim];

                for_less( i, 0, N_OUTPUT )
                {
                    if( (this_value > 0.0) == ((i & 2) != 0) &&
                        (opposite_value > 0.0) == ((i & 1) != 0) )
                    {
                        voxel = get_volume_voxel_value( output_volumes[i],
                                                v[0], v[1], v[2], v[3], v[4]);
                        ++voxel;
                        set_volume_voxel_value( output_volumes[i],
                                                v[0], v[1], v[2], v[3], v[4],
                                                voxel );
                    }
                }
            END_ALL_VOXELS

            if( n_chunk_dims == 3 )
                update_progress_report( &progress, vol );
        }

        for_less( i, 0, N_OUTPUT )
        {
            if( output_minc_volume( output_files[i] ) != OK )
                return( 1 );
        }

        if( n_chunk_dims == 2 )
            update_progress_report( &progress, slice + 1 );
    }

    terminate_progress_report( &progress );

    for_less( i, 0, N_OUTPUT )
        (void) close_minc_output( output_files[i] );

    return( 0 );
}
