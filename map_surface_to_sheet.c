#include  <internal_volume_io.h>
#include  <bicpl.h>

#define  DEFAULT_DEGREE   0

#define  BINTREE_FACTOR   0.5

private  void   map_2d_to_unit_sphere(
    int    x,
    int    nx,
    int    y,
    int    ny,
    Point  *point );

private  void  usage(
    STRING   executable )
{
    STRING  usage_str = "\n\
Usage: %s surface.obj  output.mnc nx ny [volume.mnc [degree]]\n\
\n\
     Maps a surface to a flat sheet image.  If the volume is not specified,\n\
     then uses surface curvature for colour.  Otherwise uses the volume\n\
     interpolated in the specified degree of either -1, 0, or 2.\n\n";

    print_error( usage_str, executable );
}

int  main(
    int   argc,
    char  *argv[] )
{
    STRING               surface_filename, output_filename, volume_filename;
    File_formats         format;
    polygons_struct      *surface, unit_sphere;
    int                  i, n_objects, nx, ny, x, y, point_index;
    int                  sizes[3], degree, poly, size, ind, n_done;
    Real                 min_value, max_value, value;
    Real                 weights[1000], *curvatures;
    object_struct        **objects;
    BOOLEAN              use_volume;
    Point                unit_point, on_sphere_point, centre, surface_point;
    Point                poly_points[1000], centroid;
    Real                 base_length;
    Vector               normal;
    Volume               volume, image;
    progress_struct      progress;
    STRING               dim_names[] = { MIzspace, MIyspace, MIxspace };

    /*--- get the arguments from the command line */

    initialize_argument_processing( argc, argv );

    if( !get_string_argument( NULL, &surface_filename ) ||
        !get_string_argument( NULL, &output_filename ) ||
        !get_int_argument( 0, &nx ) ||
        !get_int_argument( 0, &ny ) )
    {
        usage( argv[0] );
        return( 1 );
    }

    use_volume = get_string_argument( NULL, &volume_filename );
    if( use_volume )
        (void) get_int_argument( DEFAULT_DEGREE, &degree );

    /*--- input the surface */

     if( input_graphics_file( surface_filename, &format, &n_objects, &objects )
         != OK )
        return( 1 );

    /*--- check that the surface file contains a polyhedron */

    if( n_objects != 1 || get_object_type( objects[0] ) != POLYGONS )
    {
        print( "Surface file must contain 1 polygons object.\n" );
        return( 1 );
    }

    if( use_volume )
    {
        if( input_volume( volume_filename, 3, XYZ_dimension_names,
                          NC_UNSPECIFIED, FALSE, 0.0, 0.0,
                          TRUE, &volume, (minc_input_options *) NULL ) != OK )
            return( 1 );

        get_volume_real_range( volume, &min_value, &max_value );
    }

    /*--- get a pointer to the surface */

    surface = get_polygons_ptr( objects[0] );

    check_polygons_neighbours_computed( surface );

    if( !use_volume )
    {
        min_value = 0.0;
        max_value = 0.0;
        ALLOC( curvatures, surface->n_points );

        for_less( i, 0, surface->n_points )
            curvatures[i] = -1.0e30;

        initialize_progress_report( &progress, FALSE, surface->n_points,
                                    "Computing Curvatures" );

        n_done = 0;

        for_less( poly, 0, surface->n_items )
        {
            size = GET_OBJECT_SIZE( *surface, poly );

            for_less( i, 0, size )
            {
                point_index = surface->indices[
                                    POINT_INDEX(surface->end_indices,poly,i)];

                if( curvatures[point_index] == -1.0e30 )
                {
                    compute_polygon_point_centroid( surface, poly, i,
                                                    point_index,
                                                    &centroid,
                                                    &normal,
                                                    &base_length,
                                                    &curvatures[point_index] );

                    if( n_done == 0 || curvatures[point_index] < min_value )
                        min_value = curvatures[point_index];
                    if( n_done == 0 || curvatures[point_index] > max_value )
                        max_value = curvatures[point_index];

                    ++n_done;
                    update_progress_report( &progress, n_done );
                }
            }
        }

        terminate_progress_report( &progress );

        if( n_done != surface->n_points )
            print( "n_done != surface->n_points, %d, %d\n", n_done,
                   surface->n_points );
    }

    /*--- create a unit sphere with same number of triangles as skin surface */

    fill_Point( centre, 0.0, 0.0, 0.0 );

    create_tetrahedral_sphere( &centre, 1.0, 1.0, 1.0,
                               surface->n_items, &unit_sphere );

    create_polygons_bintree( &unit_sphere,
                             unit_sphere.n_items * BINTREE_FACTOR );

    image = create_volume( 3, dim_names, NC_SHORT, FALSE, 0.0, 0.0 );

    if( nx < 1 )
        nx = 1;

    if( ny < 1 )
        ny = 1;

    sizes[0] = 1;
    sizes[1] = ny;
    sizes[2] = nx;
    set_volume_sizes( image, sizes );

    set_volume_real_range( image, min_value, max_value );

    alloc_volume_data( image );

    initialize_progress_report( &progress, FALSE, nx, "Mapping" );

    for_less( x, 0, nx )
    {
        for_less( y, 0, ny )
        {
            map_2d_to_unit_sphere( x, nx, y, ny, &unit_point );

            poly = find_closest_polygon_point( &unit_point, &unit_sphere,
                                               &on_sphere_point );
            
            map_point_between_polygons( &unit_sphere, poly,
                                        &on_sphere_point, surface,
                                        &surface_point );

            if( use_volume )
            {
                evaluate_volume_in_world( volume,
                                          Point_x(surface_point),
                                          Point_y(surface_point),
                                          Point_z(surface_point),
                                          degree, FALSE, 0.0,
                                          &value,
                                          NULL, NULL, NULL,
                                          NULL, NULL, NULL,
                                          NULL, NULL, NULL );
            }
            else
            {
                size = get_polygon_points( &unit_sphere, poly, poly_points );

                get_polygon_interpolation_weights( &on_sphere_point, size,
                                                   poly_points, weights );

                value = 0.0;
                for_less( i, 0, size )
                {
                    ind = unit_sphere.indices[
                             POINT_INDEX(unit_sphere.end_indices,poly,i)];
                    value += weights[i] * curvatures[ind];
                }
            }

            set_volume_real_value( image, 0, y, x, 0, 0, value );
        }

        update_progress_report( &progress, x + 1 );
    }

    terminate_progress_report( &progress );

    if( output_volume( output_filename, NC_UNSPECIFIED, FALSE, 0.0, 0.0,
                       image, "Surface-to-image map.\n", NULL ) != OK )
        return( 1 );


    delete_polygons( &unit_sphere );
    delete_object_list( n_objects, objects );

    if( use_volume )
        delete_volume( volume );
    else
        FREE( curvatures );

    delete_volume( image );

    output_alloc_to_file( NULL );

    return( 0 );
}

private  void   map_2d_to_unit_sphere(
    int    i,
    int    ni,
    int    j,
    int    nj,
    Point  *point )
{
    Real  angle_around, angle_up, x, y, z, r;

    angle_around = (Real) i / (Real) (ni-1) * 2.0 * PI;
    angle_up = -PI/2 + (Real) j / (Real) (nj-1) * PI;

    z = sin( angle_up );

    r = cos( angle_up );

    x = r * cos( angle_around );
    y = r * sin( angle_around );

    fill_Point( *point, x, y, z );
}
