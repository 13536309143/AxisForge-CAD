{
    for(const auto &name:QStringList{"bearing","gear","linear-guide"}) {
        Scene example;QString why;
        const bool loaded=loadProject("examples/mechanical/"+name+".afcad",example,&why);
        check(loaded,"mechanical example loads");
        if(!loaded) continue;
        document_.replace(example);selectBody({});viewport_->setDisplayMode(Viewport::DisplayMode::Normal);
        viewport_->fitAll();
        const auto frame=viewport_->grabFramebuffer();
        check(!frame.isNull() && viewport_->renderError().isEmpty(),"mechanical example renders");
        check(frame.save(directory+"/example-"+name+".png"),"mechanical preview saved");
    }
}
