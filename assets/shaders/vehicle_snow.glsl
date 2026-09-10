// Pane coordinates are attached to the source mesh, so the swept outline
// follows steering, body damage and instancing without moving across the glass.
float windshield_snow_remaining(vec3 pane) {
    float aspect=max(pane.z,1.0f);
    vec2 p=vec2(pane.x*aspect,pane.y);
    float radius=max(.94f,aspect*.37f);
    float swept=0.0f;
    for(int i=0;i<2;++i) {
        vec2 pivot=vec2((i==0 ? .23f : .73f)*aspect,-.06f);
        vec2 delta=p-pivot;
        float angle=atan(delta.y,delta.x);
        float fan=smoothstep(.04f,.10f,angle)*(1.0f-smoothstep(2.98f,3.08f,angle));
        swept=max(swept,fan*(1.0f-smoothstep(radius-.025f,radius,length(delta))));
    }
    // A narrow rim at the seals remains snowy. No frost veil is added inside
    // the swept area: the original windshield stays fully visible there.
    float edge=min(min(pane.x,1.0f-pane.x),min(pane.y,1.0f-pane.y));
    swept*=smoothstep(.015f,.04f,edge);
    return 1.0f-swept;
}
