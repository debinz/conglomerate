#include  <internal_volume_io.h>
#include  <bicpl.h>

private  void  compute_transforms(
    int        n_surfaces,
    int        n_points,
    Point      **points,
    Transform  transforms[] );

private  void  create_average_polygons(
    int               n_surfaces,
    int               n_points,
    Point             **points,
    Transform         transforms[],
    polygons_struct   *polygons );

private  void  print_transform(
    Transform   *trans );

int  main(
    int    argc,
    char   *argv[] )
{
    Status           status;
    STRING           filename, output_filename;
    int              i, n_objects, n_surfaces;
    Colour           *colours;
    File_formats     format;
    object_struct    *out_object;
    object_struct    **object_list;
    polygons_struct  *polygons, *average_polygons;
    Point            **points_list;
    Transform        *transforms;

    status = OK;

    initialize_argument_processing( argc, argv );

    if( !get_string_argument( "", &output_filename ) )
    {
        print_error(
          "Usage: %s output.obj [input1.obj] [input2.obj] ...\n",
          argv[0] );
        return( 1 );
    }

    n_surfaces = 0;

    out_object = create_object( POLYGONS );
    average_polygons = get_polygons_ptr(out_object);

    while( get_string_argument( NULL, &filename ) )
    {
        if( input_graphics_file( filename, &format, &n_objects,
                                 &object_list ) != OK )
        {
            print( "Couldn't read %s.\n", filename );
            return( 1 );
        }

        if( n_objects != 1 || get_object_type(object_list[0]) != POLYGONS )
        {
            print( "Invalid object in file.\n" );
            return( 1 );
        }

        polygons = get_polygons_ptr( object_list[0] );

        if( n_surfaces == 0 )
        {
            copy_polygons( polygons, average_polygons );

            (void) get_object_colours( out_object, &colours );
            REALLOC( colours, polygons->n_points );
            set_object_colours( out_object, colours );
            average_polygons->colour_flag = PER_VERTEX_COLOURS;
        }
        else if( !polygons_are_same_topology( average_polygons, polygons ) )
        {
            print( "Invalid polygons topology in file.\n" );
            return( 1 );
        }

        SET_ARRAY_SIZE( points_list, n_surfaces, n_surfaces+1, 1 );
        ALLOC( points_list[n_surfaces], polygons->n_points );

        for_less( i, 0, polygons->n_points )
            points_list[n_surfaces][i] = polygons->points[i];

        ++n_surfaces;

        print( "%d:  %s\n", n_surfaces, filename );

        delete_object_list( n_objects, object_list );
    }

    ALLOC( transforms, n_surfaces );

    compute_transforms( n_surfaces, average_polygons->n_points,
                        points_list, transforms );

    create_average_polygons( n_surfaces, average_polygons->n_points,
                             points_list, transforms, average_polygons );

    compute_polygon_normals( average_polygons );

    if( status == OK )
        status = output_graphics_file( output_filename, format,
                                       1, &out_object );

    if( status == OK )
        print( "Objects output.\n" );

    return( status != OK );
}

private  void  compute_point_to_point_transform(
    int        n_points,
    Point      src_points[],
    Point      dest_points[],
    Transform  *transform )
{
    int    p, dim;
    Real   **coords, *target, coefs[4];

    ALLOC2D( coords, n_points, 3 );
    ALLOC( target, n_points );

    for_less( p, 0, n_points )
    {
        coords[p][X] = Point_x(src_points[p]);
        coords[p][Y] = Point_y(src_points[p]);
        coords[p][Z] = Point_z(src_points[p]);
    }

    make_identity_transform( transform );

    for_less( dim, 0, N_DIMENSIONS )
    {
        for_less( p, 0, n_points )
            target[p] = Point_coord(dest_points[p],dim);

        least_squares( n_points, N_DIMENSIONS, coords, target, coefs );

        Transform_elem( *transform, dim, 0 ) = coefs[1];
        Transform_elem( *transform, dim, 1 ) = coefs[2];
        Transform_elem( *transform, dim, 2 ) = coefs[3];
        Transform_elem( *transform, dim, 3 ) = coefs[0];
    }

    FREE( target );
    FREE2D( coords );
}

private  void  compute_transforms(
    int        n_surfaces,
    int        n_points,
    Point      **points,
    Transform  transforms[] )
{
    int   s;

    make_identity_transform( &transforms[0] );

    for_less( s, 1, n_surfaces )
    {
        compute_point_to_point_transform( n_points, points[s], points[0],
                                          &transforms[s] );
    }
}

private  void  create_average_polygons(
    int               n_surfaces,
    int               n_points,
    Point             **points,
    Transform         transforms[],
    polygons_struct   *polygons )
{
    Point  avg;
    Real   x, y, z;
    int    p, s;

    for_less( p, 0, n_points )
    {
        fill_Point( avg, 0.0, 0.0, 0.0 );

        for_less( s, 0, n_surfaces )
        {
            transform_point( &transforms[s],
                             Point_x(points[s][p]),
                             Point_y(points[s][p]),
                             Point_z(points[s][p]), &x, &y, &z );

            Point_x(avg) += x;
            Point_y(avg) += y;
            Point_z(avg) += z;
        }

        SCALE_POINT( polygons->points[p], avg, 1.0/(Real) n_surfaces );

        polygons->colours[p] = WHITE;
    }
}

private  void  print_transform(
    Transform   *trans )
{
    int   i, j;

    for_less( i, 0, N_DIMENSIONS )
    {
        for_less( j, 0, N_DIMENSIONS+1 )
            print( " %12g", Transform_elem(*trans,i,j) );
        print( "\n" );
    }

    print( "\n" );
}
